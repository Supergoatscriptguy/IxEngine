"""Train IxEngine's NNUE and export it in the engine's exact quantized layout.

Architecture (must match src/nnue.cpp): 768 -> HL perspective accumulators,
SCReLU, 8 piece-count output buckets. Exports int16 weights:
  ftWeights[768*HL] (feature-major) | ftBias[HL] | outWeights[8*2HL] | outBias[8]
with feature weights x QA, output weights x QB, output bias x QA*QB.

  python tools/train_nnue.py --data data/gen1.txt --out bin/ix.nnue --epochs 8

Data lines: "<FEN> | <score_cp_white> | <wdl_white>".  We convert to the
side-to-move's perspective for the target, since the net is STM-relative.
"""

import argparse
import time
import numpy as np
import torch
import torch.nn as nn
import chess

HL = 512
INPUT = 768
BUCKETS = 8
QA = 255
QB = 64
SCALE = 400
MAXP = 32  # max pieces on board


def feature_lists(board):
    wi, bi = [], []
    for sq, pc in board.piece_map().items():
        pt = pc.piece_type - 1                 # 0..5
        wrc = 0 if pc.color else 1             # white-perspective: own=white
        brc = 0 if (not pc.color) else 1       # black-perspective: own=black
        wi.append(wrc * 384 + pt * 64 + sq)
        bi.append(brc * 384 + pt * 64 + (sq ^ 56))
    return wi, bi


def load_data(path, limit):
    wI, bI, stm, bucket, score, wdl = [], [], [], [], [], []
    with open(path) as f:
        for line in f:
            parts = line.split("|")
            if len(parts) != 3:
                continue
            fen = parts[0].strip()
            try:
                board = chess.Board(fen)
            except Exception:
                continue
            wi, bi = feature_lists(board)
            n = len(wi)
            wp = wi + [INPUT] * (MAXP - n)      # pad with dummy column
            bp = bi + [INPUT] * (MAXP - n)
            wI.append(wp); bI.append(bp)
            stm.append(0 if board.turn else 1)  # 0 = white to move
            b = (n - 2) // 4
            bucket.append(min(max(b, 0), BUCKETS - 1))
            score.append(float(parts[1]))
            wdl.append(float(parts[2]))
            if len(stm) >= limit:
                break
    return (np.array(wI, np.int64), np.array(bI, np.int64), np.array(stm, np.int64),
            np.array(bucket, np.int64), np.array(score, np.float32), np.array(wdl, np.float32))


class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.ft = nn.Linear(INPUT, HL)
        self.out = nn.Linear(2 * HL, BUCKETS)

    def accum(self, idx):                       # idx [B,MAXP] -> [B,HL]
        B = idx.shape[0]
        dense = torch.zeros(B, INPUT + 1, device=idx.device)
        dense.scatter_(1, idx, 1.0)
        return self.ft(dense[:, :INPUT])

    def forward(self, wIdx, bIdx, stm, bucket):
        accW, accB = self.accum(wIdx), self.accum(bIdx)
        stm = stm.unsqueeze(1).float()
        own = accW * (1 - stm) + accB * stm     # stm==0(white) -> accW
        opp = accB * (1 - stm) + accW * stm
        x = torch.cat([torch.clamp(own, 0, 1) ** 2, torch.clamp(opp, 0, 1) ** 2], 1)
        o = self.out(x)                         # [B,BUCKETS]
        return o.gather(1, bucket.unsqueeze(1)).squeeze(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--lambda_", type=float, default=0.7, help="eval vs WDL blend")
    ap.add_argument("--max", type=int, default=2_000_000)
    args = ap.parse_args()

    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"device: {dev}  (loading {args.data} ...)")
    t0 = time.time()
    wI, bI, stm, bucket, score, wdl = load_data(args.data, args.max)
    N = len(stm)
    print(f"{N} positions loaded in {time.time()-t0:.0f}s")

    # White-relative -> side-to-move-relative target.
    stm_score = np.where(stm == 0, score, -score)
    stm_wdl = np.where(stm == 0, wdl, 1.0 - wdl)
    target = args.lambda_ * (1 / (1 + np.exp(-stm_score / SCALE))) + (1 - args.lambda_) * stm_wdl

    wI = torch.tensor(wI); bI = torch.tensor(bI); stm = torch.tensor(stm)
    bucket = torch.tensor(bucket); target = torch.tensor(target, dtype=torch.float32)

    net = Net().to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    for ep in range(args.epochs):
        perm = torch.randperm(N)
        tot = 0.0
        for i in range(0, N, args.batch):
            j = perm[i:i + args.batch]
            o = net(wI[j].to(dev), bI[j].to(dev), stm[j].to(dev), bucket[j].to(dev))
            loss = ((torch.sigmoid(o) - target[j].to(dev)) ** 2).mean()
            opt.zero_grad(); loss.backward(); opt.step()
            tot += loss.item() * len(j)
        print(f"epoch {ep+1}/{args.epochs}  loss {tot/N:.5f}")

    # Quantized export in the engine's layout.
    ftW = net.ft.weight.detach().cpu().numpy()        # [HL,768]
    ftB = net.ft.bias.detach().cpu().numpy()          # [HL]
    oW = net.out.weight.detach().cpu().numpy()        # [8,2HL]
    oB = net.out.bias.detach().cpu().numpy()          # [8]

    def q(a, s, lim=32767):
        return np.clip(np.round(a * s), -lim, lim).astype(np.int16)

    blob = np.concatenate([
        q(ftW.T.reshape(-1), QA),     # feature-major: f*HL + h
        q(ftB, QA),
        q(oW.reshape(-1), QB, 127),   # clamp to +/-127 for AVX2 madd safety
        q(oB, QA * QB),
    ])
    blob.tofile(args.out)
    print(f"wrote {args.out}  ({blob.nbytes} bytes)")


if __name__ == "__main__":
    main()

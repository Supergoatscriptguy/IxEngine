"""Train IxEngine's NNUE and export it in the engine's exact quantized layout.

Architecture (must match src/nnue.cpp): 768 -> HL perspective accumulators,
SCReLU, 8 piece-count output buckets. Exports int16 weights:
  ftWeights[768*HL] (feature-major) | ftBias[HL] | outWeights[8*2HL] | outBias[8]
feature weights x QA, output weights x QB (clamped +/-127), output bias x QA*QB.

  python tools/train_nnue.py --data data/gen1.txt --out bin/ix.nnue --epochs 20

Data lines: "<FEN> | <score_cp_white> | <wdl_white>".  Converted to the
side-to-move's perspective for the target (the net is STM-relative).
"""

import argparse
import time
import numpy as np
import torch
import torch.nn as nn

HL = 512
INPUT = 768
BUCKETS = 8
QA = 255
QB = 64
SCALE = 400
MAXP = 32
PT = {"p": 0, "n": 1, "b": 2, "r": 3, "q": 4, "k": 5}


def load_data(path, limit):
    # Pass 1: count usable lines.
    n = 0
    with open(path) as f:
        for _ in f:
            n += 1
            if n >= limit:
                break
    N = n

    wI = np.full((N, MAXP), INPUT, np.int16)
    bI = np.full((N, MAXP), INPUT, np.int16)
    stm = np.zeros(N, np.int64)
    bucket = np.zeros(N, np.int64)
    score = np.zeros(N, np.float32)
    wdl = np.zeros(N, np.float32)

    out = 0
    with open(path) as f:
        for line in f:
            if out >= N:
                break
            parts = line.split("|")
            if len(parts) != 3:
                continue
            ff = parts[0].split()
            board, turn = ff[0], ff[1]
            rank, file, k = 7, 0, 0
            for ch in board:
                if ch == "/":
                    rank -= 1; file = 0
                elif "1" <= ch <= "8":
                    file += ord(ch) - 48
                else:
                    sq = rank * 8 + file
                    white = ch.isupper()
                    pt = PT[ch.lower()]
                    wI[out, k] = (0 if white else 1) * 384 + pt * 64 + sq
                    bI[out, k] = (1 if white else 0) * 384 + pt * 64 + (sq ^ 56)
                    k += 1; file += 1
            stm[out] = 0 if turn == "w" else 1
            bucket[out] = min(max((k - 2) // 4, 0), BUCKETS - 1)
            score[out] = float(parts[1])
            wdl[out] = float(parts[2])
            out += 1
    return wI[:out], bI[:out], stm[:out], bucket[:out], score[:out], wdl[:out]


class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.ft = nn.Linear(INPUT, HL)
        self.out = nn.Linear(2 * HL, BUCKETS)

    def accum(self, idx):                       # idx [B,MAXP] int -> [B,HL]
        dense = torch.zeros(idx.shape[0], INPUT + 1, device=idx.device)
        dense.scatter_(1, idx.long(), 1.0)
        return self.ft(dense[:, :INPUT])

    def forward(self, wIdx, bIdx, stm, bucket):
        accW, accB = self.accum(wIdx), self.accum(bIdx)
        stm = stm.unsqueeze(1).float()
        own = accW * (1 - stm) + accB * stm
        opp = accB * (1 - stm) + accW * stm
        x = torch.cat([torch.clamp(own, 0, 1) ** 2, torch.clamp(opp, 0, 1) ** 2], 1)
        return self.out(x).gather(1, bucket.unsqueeze(1)).squeeze(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--lambda_", type=float, default=0.7, help="eval vs WDL blend")
    ap.add_argument("--max", type=int, default=60_000_000)
    args = ap.parse_args()

    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"device: {dev}  loading {args.data} ...")
    t0 = time.time()
    wI, bI, stm, bucket, score, wdl = load_data(args.data, args.max)
    N = len(stm)
    print(f"{N} positions in {time.time()-t0:.0f}s")

    stm_score = np.where(stm == 0, score, -score)
    stm_wdl = np.where(stm == 0, wdl, 1.0 - wdl)
    target = args.lambda_ * (1 / (1 + np.exp(-stm_score / SCALE))) + (1 - args.lambda_) * stm_wdl

    wI = torch.from_numpy(wI); bI = torch.from_numpy(bI)
    stm = torch.from_numpy(stm); bucket = torch.from_numpy(bucket)
    target = torch.tensor(target, dtype=torch.float32)

    net = Net().to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)
    for ep in range(args.epochs):
        perm = torch.randperm(N)
        tot = 0.0
        for i in range(0, N, args.batch):
            j = perm[i:i + args.batch]
            o = net(wI[j].to(dev), bI[j].to(dev), stm[j].to(dev), bucket[j].to(dev))
            loss = ((torch.sigmoid(o) - target[j].to(dev)) ** 2).mean()
            opt.zero_grad(); loss.backward(); opt.step()
            tot += loss.item() * len(j)
        sched.step()
        print(f"epoch {ep+1:>2}/{args.epochs}  loss {tot/N:.5f}  lr {sched.get_last_lr()[0]:.2e}")

    ftW = net.ft.weight.detach().cpu().numpy()
    ftB = net.ft.bias.detach().cpu().numpy()
    oW = net.out.weight.detach().cpu().numpy()
    oB = net.out.bias.detach().cpu().numpy()

    def q(a, s, lim=32767):
        return np.clip(np.round(a * s), -lim, lim).astype(np.int16)

    blob = np.concatenate([
        q(ftW.T.reshape(-1), QA),
        q(ftB, QA),
        q(oW.reshape(-1), QB, 127),
        q(oB, QA * QB),
    ])
    blob.tofile(args.out)
    print(f"wrote {args.out}  ({blob.nbytes} bytes)")


if __name__ == "__main__":
    main()

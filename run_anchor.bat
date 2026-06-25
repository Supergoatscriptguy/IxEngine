@echo off
REM One command to place IxEngine on the CCRL 40/40 scale (blitz-anchored approximation).
REM Re-run after any upgrade; results land in tools\anchor\runs\ and history.csv.
python "%~dp0tools\anchor\run_anchor.py" %*

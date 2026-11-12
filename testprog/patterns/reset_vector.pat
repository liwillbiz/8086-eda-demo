# reset_vector.pat — A1 in vector mode (design doc §6.2).
# The test sets `grade` to the grade under test; TCLCL is the period of timing set 0.
pattern reset_vector
spec    spec/spec.yaml
pins    RESET READY INTR NMI HOLD MN_MX_N  ALE RD_N WR_N M_IO_N  AD[15:0]           A[19:16]
timing  0 drive=0ns  strobe=TCLAV.max+5ns grade=8086
#       ---- drive ----------- | ---- compare ----------------------------------
v 0     1     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 4
v 0     0     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 7
v 0     0     1     0    0   0  1      H    H    H    H     HHHHHHHHHHHHLLLL    HHHH   # T1: addr FFFF0h

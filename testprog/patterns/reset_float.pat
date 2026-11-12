# reset_float.pat — C4: hold RESET so the device floats its 3-state outputs (brief §5 RESET,
# §7 C4), then the test forces voltages on them. No compares.
pattern reset_float
spec    spec/spec.yaml
pins    RESET READY INTR NMI HOLD MN_MX_N  AD[15:0]
timing  0 drive=0ns strobe=0ns
v 0     1     1     0    0   0  1        ZZZZZZZZZZZZZZZZ   repeat 12

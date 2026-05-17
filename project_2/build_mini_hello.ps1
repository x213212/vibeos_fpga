$out = "H:\testproject\project_2\mini_hello_os.bin"

function Add-WordLE {
    param([System.Collections.Generic.List[byte]]$Bytes, [Int64]$Word)
    $Word = $Word -band 0xffffffff
    $Bytes.Add([byte]($Word -band 0xff))
    $Bytes.Add([byte](($Word -shr 8) -band 0xff))
    $Bytes.Add([byte](($Word -shr 16) -band 0xff))
    $Bytes.Add([byte](($Word -shr 24) -band 0xff))
}

function RType([int]$funct7, [int]$rs2, [int]$rs1, [int]$funct3, [int]$rd, [int]$op) {
    return (($funct7 -shl 25) -bor ($rs2 -shl 20) -bor ($rs1 -shl 15) -bor ($funct3 -shl 12) -bor ($rd -shl 7) -bor $op) -band 0xffffffff
}

function IType([int]$imm, [int]$rs1, [int]$funct3, [int]$rd, [int]$op) {
    return ((($imm -band 0xfff) -shl 20) -bor ($rs1 -shl 15) -bor ($funct3 -shl 12) -bor ($rd -shl 7) -bor $op) -band 0xffffffff
}

function SType([int]$imm, [int]$rs2, [int]$rs1, [int]$funct3, [int]$op) {
    $lo = $imm -band 0x1f
    $hi = ($imm -shr 5) -band 0x7f
    return (($hi -shl 25) -bor ($rs2 -shl 20) -bor ($rs1 -shl 15) -bor ($funct3 -shl 12) -bor ($lo -shl 7) -bor $op) -band 0xffffffff
}

function BType([int]$imm, [int]$rs2, [int]$rs1, [int]$funct3) {
    $u = $imm -band 0x1fff
    return ((($u -shr 12) -shl 31) -bor (((($u -shr 5) -band 0x3f)) -shl 25) -bor ($rs2 -shl 20) -bor ($rs1 -shl 15) -bor ($funct3 -shl 12) -bor (((($u -shr 1) -band 0xf)) -shl 8) -bor (((($u -shr 11) -band 1)) -shl 7) -bor 0x63) -band 0xffffffff
}

function UType([int]$imm20, [int]$rd, [int]$op) {
    return (($imm20 -shl 12) -bor ($rd -shl 7) -bor $op) -band 0xffffffff
}

function JType([int]$imm, [int]$rd) {
    $u = $imm -band 0x1fffff
    return ((($u -shr 20) -shl 31) -bor (((($u -shr 1) -band 0x3ff)) -shl 21) -bor (((($u -shr 11) -band 1)) -shl 20) -bor (((($u -shr 12) -band 0xff)) -shl 12) -bor ($rd -shl 7) -bor 0x6f) -band 0xffffffff
}

$bytes = [System.Collections.Generic.List[byte]]::new()

# Register names used below:
# x2=sp, x5=t0 UART base, x6=t1 string pointer, x7=t2 char, x28=t3 LSR scratch.
$words = @(
    0x00000013,                 # nop; single-core bring-up variant
    0x00000013,                 # nop; reserve original mhartid/bnez slots
    (UType 0x80003 2 0x37),     # lui sp, 0x80003
    (UType 0x10000 5 0x37),     # lui t0, 0x10000
    (UType 0x80000 6 0x37),     # lui t1, 0x80000
    (IType 128 6 0 6 0x13),     # addi t1, t1, msg offset
    (IType 0 6 4 7 0x03),       # loop: lbu t2, 0(t1)
    (BType 28 0 7 0),           # beq t2, x0, done
    (IType 5 5 4 28 0x03),      # wait: lbu t3, 5(t0)
    (IType 0x40 28 7 28 0x13),  # andi t3, t3, 0x40
    (BType -8 0 28 0),          # beq t3, x0, wait
    (SType 0 7 5 0 0x23),       # sb t2, 0(t0)
    (IType 1 6 0 6 0x13),       # addi t1, t1, 1
    (JType -28 0),              # j loop
    (JType 0 0),                # done: j done
    0x10500073,                 # park: wfi
    (JType -4 0)                # j park
)

foreach ($w in $words) {
    Add-WordLE $bytes ([Int64]$w)
}

while ($bytes.Count -lt 128) {
    $bytes.Add(0)
}
[byte[]]$msg = [Text.Encoding]::ASCII.GetBytes("Hello OS!`n`0")
$bytes.AddRange($msg)
while ($bytes.Count -lt 4096) {
    $bytes.Add(0)
}
[IO.File]::WriteAllBytes($out, $bytes.ToArray())
Write-Host ("WROTE {0} bytes to {1}" -f $bytes.Count, $out)

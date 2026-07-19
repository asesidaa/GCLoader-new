# E-033: complete static audit of IFBL opcode-0x11 waits

## Inventory

A full IDA instruction scan found 34 statically initialized IFBL
opcode-`0x11` descriptors in `game471.exe`:

- 22 descriptors have value 1;
- 10 descriptors have value 0;
- 2 descriptors have value 15.

The descriptors are initialized by `sub_69D700` and `sub_699B60`. No other
static initializer in the executable stores opcode `0x11` into an IFBL
descriptor.

## Value-1 classification

Every value-1 descriptor is immediately followed by opcode `0x28` / decimal
40, which branches back to the polling label shown below. These are
cooperative next-update yields, not authored-duration pauses.

| Polling family | Value-1 descriptor address(es) |
|---|---|
| `WAIT_READ_CARD` | `0x00815868` |
| `WAIT_CARD_SELECT` | `0x00816418` |
| `SELECT_CARD_INSERT` main/yes/no | `0x008175F8`, `0x008177B0`, `0x00817968` |
| `SELECT_CARD_REISSUE` main/yes/no | `0x008187D8`, `0x00818990`, `0x00818B48` |
| `WAIT_CARD_REISSUE` | `0x008192D8` |
| `SELECT_CARD_TAKEOVER` main/yes/no | `0x00819F90`, `0x0081A148`, `0x0081A300` |
| `SELECT_USE_CARD` main/yes/no | `0x0081B1C8`, `0x0081B380`, `0x0081B538` |
| `WAIT_RECEIVE_CARD_DETAIL` | `0x0081BCC8` |
| `SELECT_NOCARD` main/yes/no | `0x0081CBE8`, `0x0081CDA0`, `0x0081CF58` |
| `CHECK_CREDIT` | `0x0081D8A0` |
| `WAIT_NEW_ENTRY_BONUS` | `0x0081DF28` |
| `WAIT_START_GAME` | `0x0081E298` |

The main selection loops combine `sub_5A3AC0` absolute-time countdowns and
`sub_5A4540` input checks. Other value-1 loops poll card/network/status
callbacks. Scaling their yield reduces the service rate of the work before
the yield and is therefore semantically wrong across the entire family.

## Authored-duration classification

The only positive values greater than 1 are two value-15 waits at
`0x008120B8` and `0x00812218`. They follow `jf_card_no` and `jf_card_yes` in
the card-name confirmation sequence. These are authored visual pauses; 15
frames is 250 ms at the original 60 Hz and should still be target-rate scaled.

The ten zero waits remain zero under either rule.

## Production rule

At the shared interpreter store `0x006309D4`:

- preserve raw values 0 and 1;
- apply target-rate duration scaling only when the raw value is greater than
  1.

This rule corrects every statically present polling loop while retaining the
two proven authored 15-frame pauses. It is the narrowest semantic split
supported by the complete static descriptor inventory.

# GCLoader Domain Context

GCLoader is a replacement `iDmacDrv32` DLL that adapts Groove Coaster's expected arcade-machine interfaces to local Windows input, audio, storage, network, and runtime-patch behavior. This glossary keeps source names aligned with the game's and vendor's domain language.

## Processes and driver surface

**Game process**:
The `game471.exe` process. It receives every GCLoader feature, including iDmac, input, audio, RFID/JVS, storage, NESYS, and runtime patches.
_Avoid_: Client, main app

**NESYS process**:
The injected `NesysService.exe` process. It receives only the NESYS and process-logging behavior selected for that process role.
_Avoid_: Service when the distinction from a Windows service matters

**iDmac**:
The driver-facing arcade hardware contract exposed through `iDmacDrv32.dll`; the repository casing follows the binary name. The vendor product is iDMAC, short for Intelligent DMA Controller.
_Avoid_: Dmac, generic DMA

**FastIO**:
The register-facing arcade input and I/O behavior reached through iDmac register operations. FastIO values include board identity, status, digital input, analog, coin-slot, and GPIO registers.
_Avoid_: Input manager, iDmac itself

## Arcade features

**Booster input**:
The logical left- and right-booster directions and buttons used by Groove Coaster. FastIO field labels such as `p1_up` are transport labels and are not physical booster names.
_Avoid_: Player-one direction when describing the logical control

**RFID/JVS**:
The emulated RFID reader and its JAMMA Video Standard serial protocol behavior exposed through the virtual COM port. RFID state, JVS framing, and Taito commands are distinct responsibilities inside this feature.
_Avoid_: Card hook as a name for the whole feature

**Test-mode storage**:
The redirection policy for Groove Coaster test-mode files and related filesystem queries. It is independent of RFID/JVS even though both currently share Kernel32 hook routing.
_Avoid_: RFID storage

**NESYS**:
The Taito network environment emulated for both the game process and NESYS process, including process launch, synthetic adapter, resolver, ping, and registry behavior.
_Avoid_: Networking when registry or process-launch behavior is also meant

**Runtime patch**:
A guarded change to the loaded Groove Coaster or NESYS executable image, identified by an RVA and expected original bytes. Runtime patches include framerate, countdown, Switch-input, and NESYS ping behavior.
_Avoid_: Hook when no detour is installed

**Exclusive audio pipeline**:
The DirectSound-compatible, miniaudio-mixed, WASAPI-exclusive output path used when the experimental audio feature is enabled.
_Avoid_: WASAPI patch when referring to the complete pipeline

## Flagged ambiguities

- **iDmac vs. DMA**: use iDmac for the driver and emulated hardware contract. Use DMA only for the generic transfer mechanism or exported DMA operations.
- **NESYS process vs. Windows service**: use NESYS process for `NesysService.exe`; do not imply that every reference describes Windows Service Control Manager behavior.
- **FastIO labels vs. booster directions**: preserve exact config/register labels at transport seams, but use booster language inside input behavior.

## Example dialogue

> Developer: Does the iDmac register Adapter own keyboard polling?
>
> Domain expert: No. The input runtime publishes booster input, FastIO turns that snapshot into register values, and the iDmac Adapter preserves the game's exported driver contract.
>
> Developer: Should test-mode storage move under RFID because they share `CreateFile` hooks?
>
> Domain expert: No. RFID/JVS and test-mode storage remain separate features; a shared Kernel32 Adapter only routes calls to them.


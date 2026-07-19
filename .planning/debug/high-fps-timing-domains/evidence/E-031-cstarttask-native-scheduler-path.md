# E-031: CStartTask native scheduler path

## Construction and registration

`sub_60D040` allocates a 296-byte `CStartTask`, constructs it through
`sub_5A4010`, and registers it with task IDs `500, 500` and name
`CStartTask`. Registration flows through `sub_453A10` to `sub_45C4B0`, which
inserts the task into the shared ordered task list.

The task system's singleton `CSystemObj` registers both render and update
subscribers. Its update subscriber is registered through `sub_456790` at
priority 10.

## Update dispatch

The update-side task traversal is
`GC120FPS_TaskList_DeleteDeferredTasks` at `0x0045C250`. On each invocation it
walks the shared task list. For a task whose node suspension/delay field at
`+0x18` is zero, it calls `sub_45C1A0`; that helper invokes virtual slot
`+0x0C` on the task object.

For the `CStartTask` vtable at `0x006FAFC0`, slot `+0x0C` is
`sub_5A40A0`. That function reaches
`GC120FPS_CSeqTaskBase_UpdateStateMachine`, whose active state 3 invokes the
IFBL state update wrapper.

## Consequence

There is no CStartTask-specific authored-60-Hz gate in this scheduler path.
The earlier hypothesis that the active `SELECT_NOCARD` countdown must be
receiving only 60 `CStartTask::Update` calls per second is not supported by
the binary and is retired. The next trace boundary is the exact descriptor
loop and the timer/display callbacks used while `SELECT_NOCARD` is active.

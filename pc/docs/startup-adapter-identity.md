# Keep the selected adapter and saved settings consistent

For a private startup fixture, pass `--adapter-description` to
`prepare_startup_display_fixture.py` with the exact `Description` returned by
the selected D3D9 adapter. The helper writes `Display/sD3DDevice` alongside the
dimensions. This is the description, not the driver name, vendor ID or an
invented GPU identity. Obtain it from the backend actually used for that run.

An omitted saved identity caused the captured PC startup to take its adapter
change path. That path releases the renderer, closes the task-pool handles,
and requests FalloutNVLauncher.exe. The port's launcher-failure return then
allowed startup to continue with a closed pool. Later, navigation waited for
an ObstacleTaskData task which had no running pool worker to complete it.
These observations explain why continuing past an absent launcher is not a
complete launcher-free startup implementation.

The helper's optional value must round-trip through an INI reader: bounded
ASCII, no line breaks, no surrounding whitespace or quote delimiters. It
creates a fresh asset-free fixture and never overwrites an existing one.
Omitting the option preserves the old resolution-only fixture bytes; that
fixture alone does not establish a valid engine startup configuration.

The Windows test reads the generated values with GetPrivateProfileStringW,
checks malformed values and overwrite rejection. These are configuration
checks, not evidence of cold EXE startup, world rendering or Switch speed.
Console startup must obtain its own selected adapter identity and retain the
user's other settings; a desktop adapter name must not be shipped as a default.

# F30 near-arrow colour candidate

The owner reported that after a manual restart with0742584, far arrows were orange at night but near arrows
remained white. This is a near-path failure report; a new mapped-code/packet capture is still needed to establish
the exact active native state. The previous zero-pen change did not select a night colour.

The F30 swatches on main6 and route8 observed7 as white and5 as orange in both lighting states, and3 as dim
orange. Index4 was white by day and orange by night in those samples. They were rectangles sent by the CarPlay
test channel, not a complete AAD arrow replay. They establish useful colour candidates, not the cause of failure.

## Narrow policy

Default, automatically measured126x77 F30 with canonical main5/6 and route7/8 uses route7 by day, route5 by
night and road3. Near road/arrow edge polygons are omitted in both states. Explicit `NBT_HUD_FILL`, manual box
pins, noncanonical handles and other profiles keep their previous behaviour. The route buffers retain the
existing074 zero-pen initialization. No native attributes on main5/6 are changed, including during near draws.

Far arrows and the accepted60px rings/captions retain their existing fill4/road3 generation, geometry and main
paint state. Far orange at night is already owner-confirmed; it is a control for the next near test. No hollow-road
proposal, arbitrary RGB command, guessed FillMode, queue change, FDL coding or new geometry is included.

`PAINT` logs identify the requested day/night mode, unified/fallback renderer, eligibility/pins, exact boxes,
target buffer and selected indices. They describe the generated batch, not proof that the cluster painted it.
Raw setters, polygons and ACKs are still required for that conclusion.

## Evidence and limits

[CarPlay's drawing code](https://github.com/st1ng/nbtevo-carplay-advanced/blob/main/src/hud.h) explicitly maps
tone roles to chosen indices and initializes its6WA pen. No hidden HUD night/palette initializer was found there.
[JayJay's HUDTerminal notes](https://github.com/JayJay3071/nbt-android-auto/blob/main/docs/CLUSTER-HUDTERMINAL.md)
describe another cluster palette and are not F30 calibration values. CarPlay pauses20ms between calls; AAD
pipelines them. This is a separate timing discriminator, not evidence of native reordering, so transport is unchanged.

Tests decode actual driver batches for7/5/3, contour omission, stages and mode redraws. A real KombiNav worker
and MOST serializer run over delayed fake DSI size/ACK responses and exercise night→day→night→day. The accepted
ring geometry/goldens and far/caption indices are independently checked. These tests do not model physical RGB,
native execution or image FillMode. Calibrated preview options expose raw indices and use illustrative RGB only.

At the next authorized stationary check, first verify the mapped build. Correlate car night input, `PAINT`, both
7/8 buffers and actual219/21B/21D/ACK packets. Compare far→near→far and the accepted ring. If an explicit5 body
is still white, compare the same thin arrow through AAD and the test-channel sender in fixed lighting, then test
ACK-serial timing separately. Do not repeat the full32-colour survey or declare this candidate physically fixed.

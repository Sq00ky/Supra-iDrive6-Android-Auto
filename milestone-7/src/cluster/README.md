# Cluster output

Vehicle profiles contain the hardware choices. Shared drivers implement the
protocols, navigation state and video playback.

| Location | Responsibility |
| --- | --- |
| `profiles/guidance.hpp` | J29/G30 Guidance endpoints, existing controller handles and build default. |
| `profiles/g30.hpp` | Verified G30 video geometry, Screen output, network addresses and metadata. |
| `profiles/video.hpp` | Select the video profile at compile time; reject unsupported selections. |
| `guidance/` | Shared SOME/IP turn-instruction client, graph builder and display implementation. |
| `guidance/graph_template.hpp` | One captured J29 reference graph, used by both turn instructions and PiP. |
| `video/` | Android Auto stream negotiation, buffering, decoder, player and process supervision. |
| `video/native/` | Native PiP control, resource acquisition and QNX Screen presentation. |
| `nav_core.*`, `nav_msg.hpp`, `display.hpp` | Navigation messages, state machine and display interface. |
| `hud_terminal.*`, `hud6wa_*`, `hudterm*` | Existing MOST HUDTerminal transport and 6WA layout. |

## Build selection

From `milestone-7`, using the normal QNX SDP 6.5 environment:

```sh
# Default: existing J29 turn instructions, without a video player.
make CLUSTER_PROFILE=j29 nbt-aad rhmiapp

# G30: same Guidance protocol, plus the verified independent map-video path.
make CLUSTER_PROFILE=g30 nbt-aad rhmiapp kombi-player cluster-inspect
```

The selected daemon is copied to `nbt-aad`; profile-specific objects and
artifacts live in `build/cluster/<profile>/`. Switching profiles does not reuse
objects built with the other profile's defines. `G30_PIP=1`, `g30-player` and
`g30-inspect` remain compatibility aliases for older build scripts.

`NBT_CLUSTER` still overrides the turn-instruction backend at runtime
(`j29`, `g30`, or `6wa`). It does not change the player's compiled hardware
profile. The live iDrive checkbox enables/disables map playback within a
video-capable build.

## Shared data, different presentations

The reference graph contains 97 startup and 6 cancellation messages captured
upstream from a J29. Its bytes and replay order are retained in one header.
The turn-instruction driver patches road names, distances, direction and lanes.
The video client creates a controller and PiP resource, attaches that resource
to a working copy of the graph, and removes the template's text/bar overlays.

G30 factory captures supplied the 800×480 backing, 620×313 visible sink and
metadata used by the video profile. A shared Guidance graph does not prove
that another vehicle supports this video configuration. J29 video is therefore
not enabled by its build profile. A new supported output needs a verified
profile and capability selection, rather than another copy of the player.

See [video/README.md](video/README.md) for playback, deployment and validation
details. Run `make check-cluster-video` for profile and playback regressions;
the existing navigation tests continue to cover the shared Guidance backend.

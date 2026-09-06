# Dependency inventory

Pinned and compatibility-tested during M0 on 2026-09-06. CMake downloads immutable source
archives at configure time and verifies each SHA-256 digest. Runtime never downloads content.

| Dependency | Version / commit | Archive SHA-256 | License | Used by |
|---|---|---|---|---|
| raylib | 6.0 / `dbc56a87da87d973a9c5baa4e7438a9d20121d28` | `81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126` | zlib | Desktop presentation only |
| EnTT | 3.15.0 / `d4014c74dc3793aba95ae354d6e23a026c2796db` | `9a6c0e1a7049615d40bbe56443d42a27703c6251a5be862de8e952b0f0f84f36` | MIT | Simulation entity registry |
| Catch2 | 3.9.1 / `644821ce28cb25d7992a4d0375b1d83214392592` | `5536f5e936466e3d0ef6350630d17d976fee2dee3cc44940e77a769cafab1904` | BSL-1.0 | Tests only |
| nlohmann/json | 3.12.0 / `55f93686c01528224f448c19128836e7df245f72` | `67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef` | MIT | Persistence boundary |
| actions/checkout | 4.2.2 / `11bd71901bbe5b1630ceea73d27597364c9af683` | GitHub-hosted action | MIT | CI source checkout only |

## Bundled asset

| Asset | Source revision | File SHA-256 | License | Use |
|---|---|---|---|---|
| Nunito SemiBold static instance (weight 650) | googlefonts/nunito `8c6a9bb9732545b9ed53f29ec5e1ab0ff53c4e6f`; generated deterministically with fonttools 4.63 | `88f8925e998bafb4daed3d3e1a46396f0f58b1dc1909884fc979de2ab4879b0a` | SIL OFL 1.1; bundled at `assets/fonts/OFL.txt` | Rounded, high-legibility desktop UI text |

Upstream license files are retained inside CMake's fetched source tree during development. A
release package must copy required notices into its attribution bundle (T016).

The `headless` preset does not declare, fetch, build, or link raylib. The current local generator
is Unix Makefiles because Ninja was not installed; the presets remain portable to the supported
CMake hosts without requiring a package installation.

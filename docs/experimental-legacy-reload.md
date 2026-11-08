# Experimental native reload development

Current runtime supports only targeted legacy JSON UI reload. Use [standalone installation](standalone-initialization.md). The old broad V5 implementation, selection API and fallback are removed in v0.4.0-beta.

The optional `CONDUIT_EXPERIMENTAL_LEGACY_RELOAD` build remains a developer-controlled prototype using the same targeted backend, exact executable guard and observed-thread arming. It is not the normal standalone installation. Do not run a controller alongside the standalone DLL.

Historical investigation, including obsolete broad-reload experiments, is retained under [reverse-engineering](reverse-engineering/1.26.60-legacy-reload-path.md). Its old commands and snapshots do not describe currently available runtime functionality.

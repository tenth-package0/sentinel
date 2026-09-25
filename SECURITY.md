# Security policy

## Supported version

Security fixes are applied to the latest commit on `main`. Sentinel is a
demonstration system and has not been audited for production trading use.

## Reporting a vulnerability

Please do not open a public issue for a vulnerability that could expose data,
corrupt decisions, bypass validation, or cause unsafe memory access. Use the
repository's private security-advisory form:

1. Open the **Security** tab on GitHub.
2. Choose **Advisories** and **Report a vulnerability**.
3. Include the affected commit, reproduction steps, expected impact, and any
   proposed mitigation.

Reports should receive an acknowledgement within seven days. A fix and public
disclosure timeline will be coordinated through the advisory.

## Security boundaries

- All feeds and CSV input are untrusted.
- The native tools do not authenticate data sources.
- The browser demo stores state in memory and does not send trades to a server.
- Benchmark generators and sample data are not production market data.
- Policy configuration must be authorized by a surrounding service in a real
  deployment; the engine itself does not implement identity or access control.

## Hardening checks

The CI workflow builds with warnings as errors and runs AddressSanitizer,
UndefinedBehaviorSanitizer, ThreadSanitizer, and a time-bounded libFuzzer job on
the ITCH decoder. These checks reduce risk but do not replace a security audit.

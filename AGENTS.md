# Core
1. Use Git for code management and commitment.
2. Use state machines for complex logical transitions when appropriate.
3. Keep frequently used constants in configuration files.
4. Implement repeated functionality through shared interfaces.

# Language Guidance

Read the [LLM generation contract](docs/llm.md) before writing LM0 programs.
Use the [language specification](docs/spec.md) and
[instruction reference](docs/instructions.md) to resolve syntax and semantics.

# Issue Reporting

Follow the [issue-reporting workflow](docs/reporting.md) for suspected LM0 bugs,
unclear diagnostics or documentation, and missing capabilities, including
problems with known workarounds. Search for existing issues and include a minimal
reproducer, environment details, exact commands, and observed output in reports.
Record the submitted issue URL in the task summary. If submission is unavailable,
preserve a local report and state **Not submitted**, with its path and the reason.

# Validation
Run `python3 -m unittest discover -s tests -v` before committing compiler changes.

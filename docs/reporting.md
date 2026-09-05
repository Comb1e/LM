# Reporting LM0 Problems

Report problems to [Comb1e/LM GitHub Issues](https://github.com/Comb1e/LM/issues)
so maintainers can reproduce them and track fixes. This includes suspected
compiler/runtime bugs, incorrect or unclear diagnostics, documentation gaps,
and missing capabilities encountered while using LM0. A workaround is useful
evidence and does not make a report unnecessary.

## Prepare a Report

1. Compare the behavior with the [specification](spec.md) and reduce the problem
   to the smallest source and command that demonstrate it. Ordinary errors in
   generated programs should be corrected first; report the diagnostic itself
   when it is incorrect or unclear. If reproduction is incomplete, say so and
   distinguish observed facts from a suspected cause.
2. Search open and closed issues for the symptom or diagnostic code. Reuse an
   existing issue when it describes the same problem; add only new evidence.
   A closed issue that reproduces on a newer version can be reported as a
   regression with a link to the earlier issue.
3. Fill in the body of the [problem report template](../.github/ISSUE_TEMPLATE/lm0_problem.md)
   and save it as `build/lm0-issue.md` in the current working project, using a
   distinct filename for each problem. Omit the template's YAML front matter.
   Keep source and logs minimal and remove credentials and private project data.

Include the LM0 version or checkout commit, Python and compiler versions,
target, exact command and relevant configuration, minimal source and inputs,
expected behavior, actual output/exit codes, and any workaround. For an ambiguous
instruction or missing capability, explain the intended operation and identify
the relevant documentation; a runnable reproducer may not apply.

These commands collect version information without reading credentials. Run
the Git command in the LM0 checkout, and use the Python/compiler executables
that reproduced the problem:

```sh
python3 -c 'import lm0; print(lm0.__version__)'
git rev-parse HEAD
python3 --version
gcc --version
gcc -dumpmachine
```

Include LM0's JSON diagnostics, compiler/sanitizer stderr, and the process exit
code when relevant. Do not replace actual output with an inferred explanation.
For suspected optimization errors, record the results at `-O 0` and `-O 2` if
available. Report which observations could not be collected.

## Submit Through GitHub

With an installed, authenticated GitHub CLI and access to the repository, use
the following commands. Replace the search text and title with the actual
symptom; the body file must contain the completed report.

```sh
gh issue list --repo Comb1e/LM --state all --search 'relevant error code or symptom'
gh issue create --repo Comb1e/LM --title '[LM0] Short description of the problem' --body-file build/lm0-issue.md
```

For new evidence on an existing issue, use its number instead of opening a
duplicate:

```sh
gh issue comment 123 --repo Comb1e/LM --body-file build/lm0-issue.md
```

The [`--body-file` option](https://cli.github.com/manual/gh_issue_create) preserves
multiline source and diagnostics without interpolating them into a shell
command. An authenticated issue-creation tool can also submit the same title
and body as structured arguments.

Without the CLI, open the [new issue page](https://github.com/Comb1e/LM/issues/new),
choose **LM0 problem or feedback** when available, and submit the completed
report. The repository template appears after this change reaches the default
branch; the same report body can also be used in a blank issue.

After a successful submission, record the returned issue URL in the task
summary. If the outcome is uncertain after a network error, check the issue
list before retrying to avoid duplicate submissions.

## When Submission Is Unavailable

If network access, authentication, repository access, or the agent's publishing
permissions prevent submission, keep the completed local report. Include its
path and the GitHub issue link in the task summary and state **Not submitted**,
with the reason. The user can submit that exact report later. A local file does
not notify maintainers and must not be described as a delivered issue.

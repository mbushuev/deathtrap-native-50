# Git workflow

- Start development on a dedicated, descriptive task branch from `main`, or continue on the existing task branch. Use names that describe the work and omit assistant or tool prefixes such as `codex/`. Keep `main` for releases and merge the task branch into `main` when preparing the release.
- Make local commits as each coherent fix or feature is ready. Keep each commit focused on one change, include its relevant tests, and use a subject that says what it fixes or adds.
- Before committing, review the staged diff and run the relevant checks. Keep unrelated fixes, refactors, and release preparation in separate commits.

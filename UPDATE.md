# Update Procedure For AI

## Activation Rule
- Do not use this document unless the user explicitly instructs you to work based on `UPDATE.md`.
- If the user does not explicitly say to use `UPDATE.md`, follow the normal repository instructions instead.

## Purpose
- This document defines a reproducible AI workflow for release-note text updates and release version updates.
- The goal is to update the Japanese source text first, then propagate the same content to the English and summary documents without changing the meaning.
- This document separates the update cycle from the git/release-close cycle.
- A request to work based on `UPDATE.md` means: complete the content/version update workflow and stop before any git operations unless the user separately gives git instructions.

## Source Of Truth
- Japanese release text:
  - `additional_files/readme_jp.txt`
- Release version values:
  - `source/tc2ch/version.h`
- Derived outputs:
  - `additional_files/readme_en.txt`
  - `CHANGELOG.md`
  - `README.md`
- Version-related references:
  - `source/tc2ch/exe/tclock.rc`
  - `source/tc2ch/dll/tcdll.rc`
  - `source/tc2ch/language/langja.rc`

## Core Rules
- Treat `additional_files/readme_jp.txt` as the source of truth for release text.
- Treat `source/tc2ch/version.h` as the source of truth for release version values.
- RC files must continue to reference the shared version macros from `source/tc2ch/version.h`.
- Do not invent new release content. Reuse the Japanese source text for the target date.
- Preserve meaning when translating to English. Make the English natural, but do not add or remove functional claims.
- Keep the writing scope limited to the target date entry only.
- Follow the repository rules in `AGENTS.md`.
- When the user explicitly instructs you to work based on `UPDATE.md`, treat `README.md` as part of the required derived outputs for that update cycle.
- Treat `update work` and `git work` as separate user intents:
  - update work = edit release/version files, verify, and stop
  - git work = commit / tag / merge or fast-forward to `main` / push as instructed
- Do not start any git operation during update work unless the user separately asks for git work.
- When the user explicitly asks for git work, execute the requested git workflow as one continuous sequence rather than stopping after an intermediate git step.
- Do not run `git commit` unless the user explicitly requested git work in the current turn.
- Do not run `git push` unless the user explicitly requested git work that includes push in the current turn.
- Keep commits small and scoped to one intent.
- Do not rewrite history or discard unrelated user changes.

## Required Inputs
- Target date.
- Target version string such as `v0.2.3.1`.

## Release Text Workflow
1. Open `additional_files/readme_jp.txt`.
2. Find the `バージョン履歴` section.
3. Find the entry for the target date.
4. Extract only that date block.
5. Copy the extracted Japanese text into a task log under `tasks/` so the working source is preserved for the current cycle.
6. Update `additional_files/readme_en.txt` with an English version of the same date block.
7. Update `CHANGELOG.md` with the same content rewritten into the local Markdown style used by existing entries.
8. Update `README.md` in the same cycle after `CHANGELOG.md` is updated.
9. For `README.md`, replace only the latest date/version summary block.
10. The `README.md` latest summary block should be a partial transplant from `CHANGELOG.md` for the same target date.

## Version Update Workflow
1. Open `source/tc2ch/version.h`.
2. Update the release version macros there first.
3. Confirm that `source/tc2ch/exe/tclock.rc`, `source/tc2ch/dll/tcdll.rc`, and `source/tc2ch/language/langja.rc` still reference the shared macros rather than duplicated literal version values.
4. If release notes are being updated in the same cycle, ensure the target version string matches the value from `source/tc2ch/version.h`.
5. Record the changed version fields and touched files in the current `tasks/` entry.

## Git And Tag Workflow
0. Enter this section only when the user has separately asked for git work.
1. Before commit, run `git status`.
2. Review `git diff` and ensure only intended files are included.
3. Confirm no line-ending-only full-file diffs were introduced.
4. Use a commit title/message that accurately reflects the changed behavior and scope.
5. Before any local merge or branch switch to `main`, make sure the current worktree is clean relative to the target branch.
6. Do not try to merge into a dirty local `main`. This can block the update cycle even when the release branch itself is already correct.
7. Preferred release-close sequence:
   - finalize and commit on the working release branch
   - push the working release branch only if explicitly requested
   - verify whether `main` is an ancestor of the working release branch
   - if yes, prefer fast-forwarding `main` to the release branch state
   - only use a real merge commit when fast-forward is not possible
8. Before any push, ensure `main` and the working release branch point to the same finalized release commit.
9. Create a tag only when explicitly requested by the user.
10. If a release tag exists for the finalized version and push is explicitly requested, push the tag too.
11. When the branch name and tag name are identical, do not use an ambiguous short refspec. Push the tag with its full ref name such as `refs/tags/v0.2.3.1`.
12. Do not push unless explicitly requested by the user. Stop after local commit/merge/tag work and wait for push instructions.
13. When push is explicitly requested, push all finalized release refs that were explicitly created for the cycle:
   - `main`
   - the latest working release branch such as `v****`
   - the matching release tag when it exists

## Intent Split Rule
- If the user asks only for an update based on `UPDATE.md`, do not commit, merge, tag, or push.
- If the user later asks for git work, continue from the already-prepared update state.
- A git-work instruction should be handled end-to-end within the requested scope:
  - commit-only request: finish the commit flow and stop
  - commit + main integration request: finish commit plus merge/fast-forward to `main`
  - commit + main + push request: finish commit, main integration, and push
- Keep `UPDATE.md` itself out of release commits unless the user explicitly asks to include it.

## Main Merge Safety Rule
- Do not switch the current dirty worktree from the release branch to `main` and then try to merge there.
- If the release branch is already finalized and `main` is its ancestor, treat the close step as a fast-forward update, not as a content merge problem.
- If local `main` is dirty but the remote relationship is safe, prefer updating the remote with `git push origin <release-branch>:main` rather than forcing a local merge through the dirty worktree.
- After the remote `main` update succeeds, align local `main` to the same commit and then clean the index/worktree to `HEAD`.
- Keep `UPDATE.md` itself uncommitted unless the user explicitly asks to commit it.

## File-Specific Rules

### `additional_files/readme_jp.txt`
- This is the master source for the release text.
- Do not rewrite unrelated older entries.
- Do not normalize or reformat the whole file unless required for a safe edit.

### `additional_files/readme_en.txt`
- Translate from the Japanese source entry.
- Keep bullet structure aligned with the Japanese source where practical.
- Use clear release-note English, not literal machine-translation English.

### `CHANGELOG.md`
- Convert the entry into Markdown that matches the existing file style.
- Preserve the original meaning.
- Keep the entry scoped to the target date only.

### `README.md`
- Update only the latest summary area.
- Expected heading format:
  - `## yyyy/mm/dd Main changes (v0.x.x.x)`
- Replace only the latest summary block for the target date/version.
- The replacement source should be the matching top entry from `CHANGELOG.md`.
- Do not append multiple latest-summary sections.

## Safety Checks
- Before editing, confirm the target date entry exists in `additional_files/readme_jp.txt`.
- If the target date entry does not exist, stop and report that the source text is missing.
- Before editing version values, confirm the target version fields exist in `source/tc2ch/version.h`.
- After editing, review diffs and confirm only the intended date block changed.
- After version edits, confirm the RC files still inherit the shared version macros correctly.
- Preserve file encoding and line endings expected by the repository.
- If Japanese text is edited, verify the file reopened cleanly and contains no replacement character (`U+FFFD`).

## Output Expectations
- `tasks/` contains a same-day working copy of the Japanese source entry.
- `additional_files/readme_en.txt` contains the translated entry.
- `CHANGELOG.md` contains the Markdown version of the same update.
- `README.md` contains the matching latest-summary block for the same target date/version.
- `source/tc2ch/version.h` is the only place where version values are directly changed.
- After update work alone, the repository may remain with uncommitted release-note/version changes waiting for separate git instructions.
- Any commit or tag created for the release is scoped to the requested work only.
- If the release/update cycle is being closed, the local result includes a merge to `main`, but network push remains pending until the user explicitly requests it.
- When push is requested, update `main` and the latest working release branch on the remote.

## Recommended AI Behavior
- Prefer minimal edits.
- Do not paraphrase technical changes beyond the source meaning.
- If a source phrase is ambiguous, keep the translation conservative.
- If the source and destination styles conflict, preserve meaning first and style second.

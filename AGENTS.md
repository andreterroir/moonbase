# Documentation

Documentation written by an agent lives only under `.agents/`. That includes READMEs, design notes, runbooks, and any other prose meant to be read as documentation.

Code comments are exempt. A comment that explains the line or block it sits on is not documentation and may stay next to the code. Do not use comments to sneak in docs that belong in `.agents/`.

## When human-written docs go stale

Files outside `.agents/` that a human wrote — including [readme.adoc](readme.adoc) — are not yours to rewrite. If one is outdated, leave the original text and add a highlight marker at the point that is wrong, for example:

```asciidoc
==No longer valid since the build changed (see link:.agents/docs/build.adoc[the build docs]).==
```

The marker says what is no longer valid and why. It may cite whatever makes that clear: a commit, a code reference, an agent-written doc, or more than one of those. An agent-written doc is a reference, not a replacement for the human text. Do not silently edit, delete, or "fix" the surrounding human prose.

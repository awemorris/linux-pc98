# Noct upstream snapshot

Origin: `https://github.com/awemorris/NoctLang.git`
Commit: `86079e47b8430a9fce4c67fab584499a3531658e`
Snapshot content SHA-256: `974632b64a40a9128270a37fff13fef7f1d8f8ffd598da53b5014913cfb4671a`

The snapshot is imported with `git subtree --squash`. Normal builds are
offline and never update this directory.

The initial M2 snapshot was placed in the review worktree without creating a
commit. Commit it with these trailers so later `git subtree pull --squash`
can locate its upstream boundary:

```text
git-subtree-dir: third_party/noct
git-subtree-split: 86079e47b8430a9fce4c67fab584499a3531658e
```

Maintainer commands:

```sh
scripts/update-noct.sh status
scripts/update-noct.sh verify
scripts/update-noct.sh update https://github.com/awemorris/NoctLang.git main
```

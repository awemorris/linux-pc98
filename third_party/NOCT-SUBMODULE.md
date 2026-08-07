# Noct submodule

Noct is included at `third_party/noct` as a git submodule of
<https://github.com/awemorris/NoctLang.git>. The superproject gitlink is the
authoritative reviewed revision. At the M12 migration it is pinned to:

```text
765df9ed88439eed91d118ab9bdbc6d442524527
```

The submodule replaces the earlier squashed source import. This keeps Noct's
rapid upstream fixes and their history visible while preventing ordinary
Boots builds from silently changing the dependency.

Initialize after cloning:

```sh
./build.sh noct init
```

Inspect and verify without network access:

```sh
./build.sh noct status
./build.sh noct verify
```

Advance to one explicitly reviewed upstream revision:

```sh
./build.sh noct update <commit-or-ref>
```

`update` fetches Noct, checks out the requested detached revision, and stages
the gitlink. Run Noct's upstream tests and the Boots verification target before
committing that change. Product, image, and release builds never fetch or
advance the submodule implicitly.

Noct retains its zlib license and copyright notices. Generic runtime, REPL,
and portability fixes belong in NoctLang first; PC-98 hardware and Boots host
glue remain in linux-pc98.

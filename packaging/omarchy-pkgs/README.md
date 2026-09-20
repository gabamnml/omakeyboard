# Submitting to the Omarchy package repository

Omarchy publishes its packages from
[omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs), which builds
`pkgbuilds/<package>/` and signs the result into `pkgs.omarchy.org`. This
directory is that recipe, kept next to the app so the two never drift.

To submit or update it:

```bash
git clone https://github.com/omacom/omarchy-pkgs
mkdir -p omarchy-pkgs/pkgbuilds/omakeyboard
cp -rL packaging/omarchy-pkgs/. omarchy-pkgs/pkgbuilds/omakeyboard/
rm omarchy-pkgs/pkgbuilds/omakeyboard/README.md

cd omarchy-pkgs/pkgbuilds/omakeyboard
makepkg -g                 # the tag's checksum, once the tag is pushed
# paste it over sha256sums, then open the pull request
```

Two things to know before opening it:

- **Builds need approval.** A pull request from someone who is not a
  repository collaborator or listed in `.github/VOUCHED.td` only gets a build
  plan until a maintainer applies the `build-approved` label.
- **Omarchy owns the recipes it checks in.** Once merged, the PKGBUILD is
  maintained there; the `upstream.watch` in `.omarchy/package.json` is what
  lets their tooling pick up new tags of this repository on its own.

`packaging/PKGBUILD` (the working tree) and `packaging/aur/PKGBUILD` (a tagged
commit, for the AUR) stay here for the other two ways to install the app.

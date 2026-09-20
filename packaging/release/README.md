# The release recipe

One PKGBUILD, two destinations. It builds a published tag's source archive,
which is what both of them require; `packaging/PKGBUILD` one level up is the
separate recipe that builds the working tree, for `./bin/install`.

Cutting a version updates three things here: `pkgver`, the checksum from
`makepkg -g`, and `.SRCINFO` from `makepkg --printsrcinfo > .SRCINFO`.

## The Omarchy package repository

Omarchy publishes from [omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs),
which builds `pkgbuilds/<package>/` and signs the result into `pkgs.omarchy.org`
— the repository already configured on every Omarchy install.

```bash
git clone https://github.com/omacom/omarchy-pkgs
mkdir -p omarchy-pkgs/pkgbuilds/omakeyboard
cp -rL PKGBUILD omakeyboard.install .omarchy omarchy-pkgs/pkgbuilds/omakeyboard/
# then open the pull request
```

Two things to know before opening it:

- **Builds need approval.** A pull request from someone who is not a repository
  collaborator or listed in `.github/VOUCHED.td` only gets a build plan until a
  maintainer applies the `build-approved` label.
- **Omarchy owns the recipes it checks in.** Once merged, the PKGBUILD is
  maintained there; `.omarchy/package.json` declares the upstream watch that
  lets their tooling pick up new tags of this repository on its own.

## The AUR

For everyone on Arch who is not on Omarchy. The AUR repository holds the
PKGBUILD, `.SRCINFO` and the install file — nothing else, and no `.omarchy/`.

```bash
git clone ssh://aur@aur.archlinux.org/omakeyboard.git aur-omakeyboard
cp -L PKGBUILD .SRCINFO omakeyboard.install aur-omakeyboard/
cd aur-omakeyboard && git add -A && git commit && git push
```

Pushing there is done with your own AUR account and SSH key.

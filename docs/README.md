# libwsv5 Developer Documentation

This folder contains documentation for package maintainers and developers.

## For End Users

**Start here:** [../GETTING_STARTED.md](../GETTING_STARTED.md) - Complete installation and usage guide.

## For Maintainers & Developers

### Quick Links

- **[PACKAGING.md](PACKAGING.md)** - How to create .deb packages and manage versions
- **[DISTRIBUTION_GUIDE.md](DISTRIBUTION_GUIDE.md)** - Distribution strategy and channels
- **[DEPLOYMENT_SUMMARY.md](DEPLOYMENT_SUMMARY.md)** - Overview of the deployment system
- **[PRODUCTION_CHECKLIST.md](PRODUCTION_CHECKLIST.md)** - Pre-release quality checklist
- **[INSTALL_GUIDE.md](INSTALL_GUIDE.md)** - Detailed installation instructions

## GitHub Workflows

See `.github/workflows/` for automated release workflows:
- `package-release.yml` - Automatically builds and releases packages when tags are pushed

## Project Structure

```
libwsv5/
├── README.md                 # Project overview
├── GETTING_STARTED.md        # Installation and usage guide
├── CHANGELOG.md              # Version history
├── SECURITY.md               # Security policy
├── CONTRIBUTING.md           # Contribution guidelines
├── API_REFERENCE.md          # Complete API documentation
├── LICENSE                   # MIT License
├── CMakeLists.txt            # Build system
├── libwsv5.pc.in            # pkg-config configuration
├── libwsv5.h                # Header file
├── libwsv5.c                # Implementation
├── docs/                    # This folder - Developer docs
│   ├── PACKAGING.md
│   ├── DISTRIBUTION_GUIDE.md
│   ├── DEPLOYMENT_SUMMARY.md
│   ├── PRODUCTION_CHECKLIST.md
│   └── INSTALL_GUIDE.md
├── scripts/                 # Installation & build scripts
│   ├── quick-install.sh     # One-command installer
│   ├── install-local.sh     # Local user installation
│   └── build-packages.sh    # Package builder
├── tests/                   # Test suite
│   └── test.c
└── .github/
    ├── workflows/
    │   └── package-release.yml
    └── RELEASE_TEMPLATE.md  # Release notes template
```

## Release Process

1. Update version in `CMakeLists.txt`
2. Update `CHANGELOG.md` with changes
3. Update `.github/RELEASE_TEMPLATE.md` with release notes
4. Create git tag: `git tag -a v1.1.0 -m "Release version 1.1.0"`
5. Push tag: `git push origin v1.1.0`
6. GitHub Actions automatically builds and releases!

See [DEPLOYMENT_SUMMARY.md](DEPLOYMENT_SUMMARY.md) for complete details.

## Support Resources

- **User Installation:** [../GETTING_STARTED.md](../GETTING_STARTED.md)
- **API Documentation:** [../API_REFERENCE.md](../API_REFERENCE.md)
- **Contributing:** [../CONTRIBUTING.md](../CONTRIBUTING.md)
- **Bug Reports:** GitHub Issues
- **Security Issues:** [../SECURITY.md](../SECURITY.md)

## Core Files (Root Level - Don't Move)

These files are in the project root and should stay there:

- `README.md` - Main project overview
- `GETTING_STARTED.md` - Installation guide (user-facing)
- `CHANGELOG.md` - Version history
- `SECURITY.md` - Security policy
- `CONTRIBUTING.md` - Contribution guidelines
- `LICENSE` - MIT License
- `API_REFERENCE.md` - API documentation

Developer-focused documentation has been moved to this `docs/` folder to keep the root clean and focus user attention on the essential files.

## Quick Reference

### For Users
- Installation: `../GETTING_STARTED.md`
- API Help: `../API_REFERENCE.md`
- Contributing: `../CONTRIBUTING.md`

### For Maintainers
- Packaging: `PACKAGING.md`
- Distribution: `DISTRIBUTION_GUIDE.md`
- Pre-Release: `PRODUCTION_CHECKLIST.md`
- Release Status: `DEPLOYMENT_SUMMARY.md`
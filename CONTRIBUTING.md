# Contributing to libwsv5

Thank you for your interest in contributing to libwsv5! This document provides guidelines for contributing to the project. Please review the following sections before submitting pull requests.

## Code of Conduct

We are committed to providing a welcoming and inclusive environment for all contributors.

## How to Contribute

### Reporting Bugs

1. **Check existing issues** - Search to ensure the bug hasn't been reported
2. **Provide details**:
   - Library version
   - OBS Studio version
   - Operating system and version
   - Steps to reproduce
   - Expected vs. actual behavior
   - Error messages or logs

### Suggesting Enhancements

1. **Describe the use case** - Explain why this feature would be useful
2. **Provide examples** - Show how you'd like to use the feature
3. **Consider alternatives** - Are there workarounds?

### Pull Requests

1. **Fork the repository** on GitHub
2. **Create a feature branch**: `git checkout -b feature/description-of-feature`
3. **Make your changes** with clear, descriptive commits explaining the reasoning
4. **Write tests** if adding new functionality
5. **Test locally** before submitting with `./test`
6. **Submit PR** with detailed description of changes and motivation

## Development Setup

```bash
# Install dependencies
sudo apt-get install build-essential cmake libwebsockets-dev libcjson-dev libssl-dev

# Clone the repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Build
mkdir build && cd build
cmake -DBUILD_TESTS=ON ..
make

# Run tests
./test -h localhost -p 4455 -w obs_password
```

## Code Style Guidelines

- **C Standard**: C11
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Prefer under 100 characters
- **Comments**: Clear and explanatory, especially for complex logic
- **Function Names**: snake_case (e.g., `obsws_connect`)
- **Types**: PascalCase_t for typedefs (e.g., `obsws_connection_t`)
- **Constants**: UPPER_CASE (e.g., `OBSWS_MAX_PENDING_REQUESTS`)

## Commit Messages

- **Format**: `Type: Brief description (50 chars max)`
- **Types**: `feat:`, `fix:`, `docs:`, `style:`, `test:`, `chore:`
- **Example**: `fix: handle malloc failures in base64_encode`

## Testing

- Run the full test suite before submitting
- Add tests for new features
- Ensure all existing tests still pass
- Test with different configurations (SSL, auth, multi-connection)

## Documentation

- Update README.md for user-facing changes
- Update API_REFERENCE.md for API changes
- Add entry to CHANGELOG.md in the Unreleased section
- Write clear Doxygen comments for new functions

## License

By contributing to libwsv5, you agree that your contributions will be licensed under the MIT License. Contributions should maintain the project's code quality standards and follow existing conventions.

## Questions?

Feel free to open an issue to ask questions or discuss ideas before starting major work.

Thank you for contributing to libwsv5!
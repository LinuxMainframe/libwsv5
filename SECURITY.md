# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in libwsv5, please report it responsibly by emailing the maintainer directly instead of using the public issue tracker.

**Email**: Please check the repository for contact information of Aidan A. Bradley

**What to include:**
1. Description of the vulnerability
2. Steps to reproduce (if applicable)
3. Potential impact
4. Suggested fix (if you have one)

## Security Considerations

### Authentication
- Passwords are SHA256-hashed with salt and challenge-response protocol
- Passwords are never transmitted in plain text over the network
- When using `OBSWS_DEBUG_HIGH` log level, passwords MAY appear in debug output
  - **WARNING**: Never use DEBUG_HIGH in production or with sensitive data

### TLS/SSL Support
- Use `config.use_ssl = true` for encrypted connections (WSS protocol)
- Requires OBS configured to listen on secure port (usually 4454)
- Recommended for remote connections over untrusted networks

### Thread Safety
- All public APIs are thread-safe
- Multiple threads can call library functions simultaneously
- Internal mutexes protect shared state

### Memory Safety
- Library handles all memory allocation and deallocation internally
- Response objects must be freed with `obsws_response_free()`
- Buffers are allocated at connection time with configurable sizes
- No buffer overflow vulnerabilities in public API

### Known Limitations
- WebSocket fragmentation is handled internally
- Message size limit is 64KB (configurable)
- Request ID limit is 256 in-flight requests

## Security Updates

We take security seriously. Security issues will be:
1. Acknowledged within 24 hours
2. Investigated and assessed
3. Fixed in the next patch release
4. Credited to the reporter (unless anonymity is requested)

## Best Practices for Users

1. **Always validate** responses from OBS
2. **Use SSL/TLS** for remote connections
3. **Protect the password** - treat it like a database credential
4. **Monitor logs** - set log level to OBSWS_LOG_WARNING or OBSWS_LOG_ERROR in production
5. **Never use OBSWS_DEBUG_HIGH** in production environments
6. **Update regularly** - keep libwsv5 up to date with security patches
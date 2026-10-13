//! The single error type used across the crate.

use std::fmt;

/// Convenience alias for results produced by this crate.
pub type Result<T> = std::result::Result<T, Error>;

/// Everything that can go wrong inside the Rust core.
#[derive(Debug)]
pub enum Error {
    /// A filesystem, device or syscall error.
    Io(std::io::Error),
    /// Malformed JSON crossed a module or FFI boundary.
    Json(serde_json::Error),
    /// A null pointer, invalid UTF-8 or out-of-range argument.
    InvalidInput(String),
    /// The running kernel does not provide the required interface.
    Unsupported(&'static str),
    /// A path, application id or configuration key does not exist.
    NotFound(String),
    /// A kernel interface answered, but with data we cannot interpret.
    System(String),
}

impl Error {
    /// Builds an [`Error::InvalidInput`] from anything printable.
    pub fn invalid<T: fmt::Display>(message: T) -> Self {
        Error::InvalidInput(message.to_string())
    }

    /// Builds an [`Error::System`] from anything printable.
    pub fn system<T: fmt::Display>(message: T) -> Self {
        Error::System(message.to_string())
    }

    /// Builds an [`Error::NotFound`] from anything printable.
    pub fn not_found<T: fmt::Display>(message: T) -> Self {
        Error::NotFound(message.to_string())
    }

    /// Negative errno-style code returned by the `i32` entry points of the C
    /// ABI. `0` always means success, so C++ can test with `< 0`.
    pub fn code(&self) -> i32 {
        match self {
            Error::Io(io) => -(io.raw_os_error().unwrap_or(5)),
            Error::Json(_) => -84,      // EILSEQ
            Error::InvalidInput(_) => -22, // EINVAL
            Error::Unsupported(_) => -38,  // ENOSYS
            Error::NotFound(_) => -2,      // ENOENT
            Error::System(_) => -5,        // EIO
        }
    }

    /// Short, stable machine readable tag, useful in JSON payloads.
    pub fn kind(&self) -> &'static str {
        match self {
            Error::Io(_) => "io",
            Error::Json(_) => "json",
            Error::InvalidInput(_) => "invalid-input",
            Error::Unsupported(_) => "unsupported",
            Error::NotFound(_) => "not-found",
            Error::System(_) => "system",
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => write!(f, "i/o error: {e}"),
            Error::Json(e) => write!(f, "json error: {e}"),
            Error::InvalidInput(m) => write!(f, "invalid input: {m}"),
            Error::Unsupported(m) => write!(f, "unsupported: {m}"),
            Error::NotFound(m) => write!(f, "not found: {m}"),
            Error::System(m) => write!(f, "system error: {m}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Io(e) => Some(e),
            Error::Json(e) => Some(e),
            _ => None,
        }
    }
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<serde_json::Error> for Error {
    fn from(e: serde_json::Error) -> Self {
        Error::Json(e)
    }
}

impl From<std::str::Utf8Error> for Error {
    fn from(e: std::str::Utf8Error) -> Self {
        Error::InvalidInput(format!("not valid utf-8: {e}"))
    }
}

impl From<std::string::FromUtf8Error> for Error {
    fn from(e: std::string::FromUtf8Error) -> Self {
        Error::InvalidInput(format!("not valid utf-8: {e}"))
    }
}

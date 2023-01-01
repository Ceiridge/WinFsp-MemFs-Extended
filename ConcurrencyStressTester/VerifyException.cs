namespace ConcurrencyStressTester;

public class VerifyException : Exception {
	public VerifyException(string filePath, string message, dynamic? actual = null, dynamic? expected = null) : base($"[{filePath}] {message}; Expected: {expected}; Actual: {actual}") {
	}
}

namespace ConcurrencyStressTester;

public abstract class Procedure {
	private DirectoryInfo? internalDir;

	public virtual int Threads => 5;

	/// <summary>
	/// Called in a loop from multiple threads
	/// </summary>
	/// <returns>A state that will be verified later</returns>
	public abstract dynamic Act(int threadId, int iteration);

	/// <summary>
	/// Will throw an exception on failure
	/// </summary>
	public abstract void Verify(dynamic state, int threadId, int iteration);

	public virtual void Prepare() {
		this.InternalDirectory(); // Create it
		Console.WriteLine($"[{this.GetType().Name}] Folder: {this.internalDir!.Name}");
	}

	protected DirectoryInfo InternalDirectory() {
		if (this.internalDir is not null) {
			return this.internalDir;
		}

		this.internalDir = new DirectoryInfo(RandomString());
		this.internalDir.Create();
		return this.internalDir;
	}

	protected string GetFullFileName(string fileName) {
		return Path.Combine(this.InternalDirectory().FullName, fileName);
	}

	protected static string RandomString(int length = 16) {
		char[] chars = new char[length];

		for (int i = 0; i < length; i++) {
			chars[i] = (char)Random.Shared.Next('a', 'z');
		}

		return new string(chars);
	}
}

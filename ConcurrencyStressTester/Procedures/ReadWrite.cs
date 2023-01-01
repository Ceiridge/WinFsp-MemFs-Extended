namespace ConcurrencyStressTester.Procedures;

public class ReadWrite : Procedure {
	private readonly List<string> filePaths = new List<string>();
	private const int FILES = 5;
	private const int FILE_SIZE = 500000 - 1; // ~500KB
	private const int SECTION_SIZE = 16;

	public override int Threads => FILES * 3;

	public override void Prepare() {
		base.Prepare();

		this.filePaths.Clear();
		for (int i = 0; i < FILES; i++) {
			string filePath = this.GetFullFileName(RandomString());
			File.WriteAllBytes(filePath, new byte[FILE_SIZE]);

			this.filePaths.Add(filePath);
		}
	}

	public override dynamic Act(int threadId, int iteration) {
		Span<byte> rnd = stackalloc byte[SECTION_SIZE];
		Random.Shared.NextBytes(rnd);

		foreach (string filePath in this.filePaths) {
			using FileStream stream = File.Open(filePath, FileMode.Open, FileAccess.Write, FileShare.ReadWrite);
			long? position = this.GetPosition(threadId, iteration);

			if (!position.HasValue) {
				return null!;
			}

			stream.Seek(position.Value, SeekOrigin.Begin);

			stream.Write(rnd);

			stream.Flush(true);
			VerifyRead(filePath, position.Value, rnd);
		}

		return Tuple.Create(rnd.ToArray());
	}

	public override void Verify(dynamic state, int threadId, int iteration) {
		long? position = this.GetPosition(threadId, iteration);
		if (!position.HasValue) {
			return;
		}

		var tuple = state as Tuple<byte[]>;
		byte[] rnd = tuple!.Item1;

		foreach (string filePath in this.filePaths) {
			VerifyRead(filePath, position.Value, rnd);
		}
	}

	private static void VerifyRead(string filePath, long position, ReadOnlySpan<byte> rnd) {
		using FileStream readStream = File.Open(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);

		readStream.Seek(position, SeekOrigin.Begin);
		Span<byte> actual = stackalloc byte[SECTION_SIZE];
		int read;
		if ((read = readStream.Read(actual)) != SECTION_SIZE) {
			throw new VerifyException(filePath, "Imm. Read less than section size", read, SECTION_SIZE);
		}

		if (!actual.SequenceEqual(rnd)) {
			throw new VerifyException(filePath, "Imm. Read values not equal", actual.ToArray(), rnd.ToArray());
		}
	}

	private long? GetPosition(int threadId, int iteration) {
		long position = (SECTION_SIZE * threadId) + (iteration * (SECTION_SIZE * this.Threads));

		if (position > FILE_SIZE - SECTION_SIZE - 1) {
			return null;
		}

		return position;
	}
}

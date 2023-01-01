namespace ConcurrencyStressTester.Procedures;

public class FileRenamer : Procedure {
	private static readonly byte[] EMPTY = Array.Empty<byte>();

	public override dynamic Act(int threadId = 0, int iteration = 0) {
		string origFilePath = this.GetFullFileName(RandomString());
		string newFilePath = this.GetFullFileName(RandomString());

		File.WriteAllBytes(origFilePath, EMPTY);
		Thread.Sleep(Random.Shared.Next(1, 5));
		File.Move(origFilePath, newFilePath, false);

		return Tuple.Create(origFilePath, newFilePath);
	}

	public override void Verify(dynamic state, int threadId = 0, int iteration = 0) {
		var tuple = state as Tuple<string, string>;
		string origFilePath = tuple!.Item1;
		string newFilePath = tuple.Item2;

		if (File.Exists(origFilePath)) {
			throw new VerifyException(origFilePath, "Renamed file still exists", origFilePath, newFilePath);
		}

		if (!File.Exists(newFilePath)) {
			throw new VerifyException(newFilePath, "Renamed file does not exist", origFilePath, newFilePath);
		}

		File.Delete(newFilePath);
		if (File.Exists(newFilePath)) {
			throw new VerifyException(newFilePath, "File exists after cleanup");
		}
	}
}

namespace ConcurrencyStressTester.Procedures;

public class FileCreator : Procedure {
	public override dynamic Act(int threadId = 0, int iteration = 0) {
		string fileName = RandomString();
		byte[] randomData = new byte[Random.Shared.Next(1, 20)];
		Random.Shared.NextBytes(randomData);

		File.WriteAllBytes(this.GetFullFileName(fileName), randomData);
		return Tuple.Create(fileName, randomData);
	}

	public override void Verify(dynamic state, int threadId = 0, int iteration = 0) {
		var tuple = (Tuple<string, byte[]>)state;
		string filePath = this.GetFullFileName(tuple.Item1);

		byte[] read = File.ReadAllBytes(filePath);

		if (!read.SequenceEqual(tuple.Item2)) {
			throw new VerifyException(filePath, "Bytes not equal", read, tuple.Item2);
		}

		File.Delete(filePath);
		if (File.Exists(filePath)) {
			throw new VerifyException(filePath, "File exists after cleanup");
		}
	}
}

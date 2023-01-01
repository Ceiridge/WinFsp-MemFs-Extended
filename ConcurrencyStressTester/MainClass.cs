using ConcurrencyStressTester.Procedures;

namespace ConcurrencyStressTester;

public class MainClass {
	private static readonly List<Procedure> PROCEDURES = new List<Procedure> {
		new FileCreator(),
		new FileRenamer(),
		new ReadWrite()
	};

	public static void Main(string[] args) {
		if (args.Length != 2) {
			Console.Error.WriteLine("Invalid syntax!");
			Console.Error.WriteLine("CST.exe <DurationSec> <Comma-separated list of procedure indices/all>");
			return;
		}

		TimeSpan duration = TimeSpan.FromSeconds(int.Parse(args[0]));

		bool testAll = args[1] == "all";
		HashSet<int> wantedIndices = new HashSet<int>();
		if (!testAll) {
			foreach (string procedureIndexStr in args[1].Split(',')) {
				try {
					wantedIndices.Add(int.Parse(procedureIndexStr));
				} catch (Exception ex) {
					Console.Error.WriteLine(ex);
					Console.Error.WriteLine("Invalid index: " + procedureIndexStr);
				}
			}
		}

		List<Procedure> selectedProcedures = new List<Procedure>();
		Console.WriteLine("Available stress tests:");

		int index = 0;
		foreach (Procedure procedure in PROCEDURES) {
			bool selected = testAll || wantedIndices.Contains(index);
			if (selected) {
				selectedProcedures.Add(procedure);
			}

			Console.WriteLine($"{index}: {procedure.GetType().Name}" + (selected ? " [Selected]" : ""));
			index++;
		}

		Console.WriteLine();
		Console.WriteLine($"Starting stress test in {Environment.CurrentDirectory}. Press enter to continue");
		Console.ReadLine();
		Console.WriteLine();

		DateTimeOffset start = DateTimeOffset.UtcNow;

		ProcedureRunner runner = new ProcedureRunner(selectedProcedures);
		runner.Run(duration);

		Console.WriteLine($"Successfully done after {(DateTimeOffset.UtcNow - start).Duration()}");
	}
}

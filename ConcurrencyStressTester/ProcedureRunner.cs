using ConcurrencyStressTester.Procedures;

namespace ConcurrencyStressTester; 

public class ProcedureRunner {
	private readonly IList<Procedure> procedures;

	public ProcedureRunner(IList<Procedure> procedures) {
		this.procedures = procedures;
	}

	public void Run(TimeSpan duration) {
		Console.WriteLine($"Running for {duration}");
		
		DateTimeOffset start = DateTimeOffset.UtcNow;
		DateTimeOffset end = start + duration;
		List<Thread> threads = new List<Thread>();

		foreach (Procedure procedure in this.procedures) {
			procedure.Prepare();

			for (int i = 0; i < procedure.Threads; i++) {
				int finalThreadId = i;
				Thread runner = new Thread(() => {
					RunThread(procedure, end, finalThreadId);
				});

				runner.Start();
				threads.Add(runner);
			}
		}

		Console.WriteLine($"Running {threads.Count} threads");
		foreach (Thread thread in threads) {
			thread.Join(duration);
		}

		Console.WriteLine();
		Console.WriteLine("All procedures have been executed.");
	}

	private static void RunThread(Procedure procedure, DateTimeOffset end, int threadId) {
		List<dynamic> states = new List<dynamic>();
		int iteration = 0;

		while (DateTimeOffset.UtcNow < end) {
			try {
				states.Add(procedure.Act(threadId, iteration++));
				Thread.Sleep(Random.Shared.Next(1, 20));
			} catch (Exception ex) {
				HandleException(procedure.GetType().Name, ex);
			}
		}

		iteration = 0;

		foreach (dynamic state in states) {
			try {
				procedure.Verify(state, threadId, iteration++);
			} catch (Exception ex) {
				HandleException(procedure.GetType().Name, ex);
			}
		}
	}

	private static void HandleException(string procedureType, Exception ex) {
		Console.Error.WriteLine($"[{procedureType}] An error has been encountered:");
		Console.Error.WriteLine(ex);
		Console.Error.WriteLine(ex.StackTrace);
		Environment.Exit(1);
	}
}

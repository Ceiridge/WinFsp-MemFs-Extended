If you want Memfs to automatically start when you boot / log into your account, do the following:

1. Create a scheduled task to automatically launch memefs when logging in
	1.1. Open the Task Scheduler
	1.2. Create a new scheduled task
	1.3. Execute with highest privileges, but with your user account
	1.4. Add a trigger that triggers on logon (with your user account)
	1.5. Add a new action with the following settings:
	```
	Program/Script: "C:\Program Files (x86)\WinFsp\bin\launchctl-x64.exe"
	Arguments: start Memefs ramdisk "" \\.\R:
	```
	1.6. Optionally disable *Only run if the computer is connected to a power supply* in the task settings
	1.7. Save, run and test

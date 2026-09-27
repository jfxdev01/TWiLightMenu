#pragma once

// "Installation" page of the settings: shows how TWiLight Menu++ is started,
// helps making it start automatically and restores the initial state.
//
// No NAND write is ever done here. Installing or removing Unlaunch is left
// to the official Unlaunch installer, which is only opened after the user
// confirms.

void opt_install_status(void);
void opt_install_permanent(void);
void opt_install_restore(void);
void opt_install_restore_backup(void);
void opt_open_hub(void);

bool installSettingsBackupFound(void);
bool installHubFound(void);

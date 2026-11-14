// coop/config/config_registry_rows_dev_disc.inc -- the [dev] rows of the disc drills (the full disc's ceiling, the
// module row's intent). Included by config_registry_rows_dev.inc after the cremator rows. The entry forms are
// config_registry_rows.inc's.

CFG_FLAG(disc_ceiling_32k, "disc_ceiling_32k", "dev", false, "VOTVCOOP_DISC_CEILING_32K",
    "Developer red arm of the full-disc drill: never set. A disc's record and slot ceiling is 32 KB again, as it was "
    "before the transport's own maximum was measured to carry a disc of 32 writes, so a full disc is refused at the "
    "peer that holds it and does not cross.")
CFG_FLAG(module_intent_off, "module_intent_off", "dev", false, "VOTVCOOP_MODULE_INTENT_OFF",
    "Developer red arm of the module-row drill: never set. This client's press on an upgrade panel's module row runs "
    "on this machine alone, as it did before the press was an intent: the host never takes it, and its next balance "
    "gives the charge back.")
CFG_FLAG(starter_suitcase_every_join, "starter_suitcase_every_join", "dev", false, "VOTVCOOP_STARTER_SUITCASE_EVERY_JOIN",
    "Drill aid (host): every join is given a starter suitcase, not a first join only, so a rig whose client "
    "has joined the save before can test the lane.")
CFG_FLAG(suitcase_probe, "suitcase_probe", "dev", false, "VOTVCOOP_SUITCASE_PROBE",
    "Probe (either peer): prints the starter suitcase class's loot members and contents, for its defaults and "
    "every suitcase of the world, then spawns one in front of the player and prints it five seconds on. "
    "Lines tagged [SUITCASE-PROBE].")

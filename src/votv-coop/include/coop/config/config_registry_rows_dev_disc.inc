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
CFG_ENUM(disc_eject_drill, "disc_eject_drill", "dev", "off", "off|host|client|zip|nofix",
    "VOTVCOOP_DISC_EJECT_DRILL",
    "Drill (BOTH peers): a disc edited in the PC and ejected. The host seeds a floppy with three marked rows "
    "and 20 writes; the acting peer (the host; the client for `client`) inserts it, drops its first row, adds "
    "one, spends a write and ejects; the disc must then hold the edited rows and 19 writes on both peers. "
    "`zip`: the same with a zip drive. `nofix`: the host acts with the game fix off, the control arm, which "
    "must FAIL. One `[DISC-EJECT-DRILL] [H|C] PASS` or `FAIL` line from the acting peer. Leaves the disc in "
    "the world: a throwaway save only.")
CFG_ENUM(food_sleep_drill, "food_sleep_drill", "dev", "off", "off|on|nofix", "VOTVCOOP_FOOD_SLEEP_DRILL",
    "Drill (each peer on its own player): sleep set to 40, then the game's addFood with sleep 20 at coffee "
    "tolerance 0 (60 wanted) and 0.5 (50 wanted), then a refused eat at full hunger (40 wanted); the stats are "
    "put back. `nofix`: the same with the game fix off, the control arm, which must FAIL. One "
    "`[FOOD-SLEEP-DRILL] [H|C] PASS` or `FAIL` line per peer.")
CFG_STRING(event_fire_drill, "event_fire_drill", "dev", "", "VOTVCOOP_EVENT_FIRE_DRILL", false,
    "Drill (BOTH peers): `<name>[:<seconds>]`. Once a client stands in its world the host fires the named "
    "event of the dev event table (the F1 menu's list) through the menu's own dispatch, then the run stays up "
    "for the seconds given (60 by default) so both logs record what each lane did with it. Lines tagged "
    "[EVENT-FIRE-DRILL]; the host's DONE line ends it. No verdict of its own.")
CFG_STRING(media_url_probe, "media_url_probe", "dev", "", "VOTVCOOP_MEDIA_URL_PROBE", false,
    "Probe (either peer): web addresses, `|` between them. Twenty seconds after the world is up the gamemode's "
    "sixteenth media channel is asked to open each in turn, and the player is read once a second for 25 "
    "seconds. Lines tagged [MEDIA-URL-PROBE]; each address ends PLAYED or NOT-PLAYED.")
CFG_FLAG(starter_suitcase_every_join, "starter_suitcase_every_join", "dev", false, "VOTVCOOP_STARTER_SUITCASE_EVERY_JOIN",
    "Drill aid (host): every join is given a starter suitcase, not a first join only, so a rig whose client "
    "has joined the save before can test the lane.")
CFG_FLAG(boar_hit_drill, "boar_hit_drill", "dev", false, "VOTVCOOP_BOAR_HIT_DRILL",
    "Drill (BOTH peers): the host spawns a gray boar in front of the client, the client takes an axe and swings at "
    "its copy; the host's boar must lose health to judged hits. One `[BOAR-HIT-DRILL] [H] PASS` or `FAIL` line.")
CFG_FLAG(suitcase_probe, "suitcase_probe", "dev", false, "VOTVCOOP_SUITCASE_PROBE",
    "Probe (either peer): prints the starter suitcase class's loot members and contents, for its defaults and "
    "every suitcase of the world, then spawns one in front of the player and prints it five seconds on. "
    "Lines tagged [SUITCASE-PROBE].")
CFG_STRING(tv_link_drill, "tv_link_drill", "dev", "", "VOTVCOOP_TV_LINK_DRILL", false,
    "Drill (BOTH peers): a web address. Each peer powers the televisions of its world; the host spawns a set when "
    "the save holds none and asks it for the address through the TV link lane; each peer reads its set's play "
    "time for forty seconds. One `[TV-LINK-DRILL] [H|C] PASS` or `FAIL` line per peer.")
CFG_STRING(host_place, "host_place", "dev", "", "VOTVCOOP_HOST_PLACE", false,
    "Drill aid (host): `x,y,z`. The host's own player is put there once its world is up, before anyone joins, so "
    "a drill whose client joins at the host and walks starts from a known spot. One [HOST-PLACE] line.")
CFG_STRING(event_fire_walk, "event_fire_walk", "dev", "", "VOTVCOOP_EVENT_FIRE_WALK", false,
    "Drill aid (host), with event_fire_drill: `x,y,z`. Five seconds after the fire the host's player is put there, "
    "for an event whose actor waits for a player to walk into it (the Rozital hole). One [EVENT-FIRE-DRILL] line.")

// coop/dev/cheat_drill.h -- drill: the host's and the players' cheat tools, end to end on two peers (ini cheat_drill=
// off|on / env VOTVCOOP_CHEAT_DRILL; BOTH peers, one run). The host runs it from the first line, the client from its
// join; they sequence on lobby-chat lines (coop/dev/drill_chat).
//
// First each peer opens the F1 panes it is offered and waits until the render thread drew each (the host: Items,
// Economy, Events, Entities, World, Myself, Player cheats; the client, with the host's setting off: Myself, and neither
// Items nor the host's category is offered), then:
//   A  the host spawns two crowbars through the Items call, and the client's own game gains two.
//   B  the host adds 777 points through the Economy call; the host's balance moves by it and the client's mirrors it.
//   C  the host fires the event arirGraff_3 through the Events call; the host's watch commits a fire and the client
//      replays it.
//   D  with the host's setting off, the client asks for each cheat: every one is answered Off, and the host's balance and
//      crowbars did not move.
//   E  the host turns the setting on (the `set` line its checkbox submits); the client sees it, opens Items, Economy and
//      Events, and asks for +321 points (Ok), two crowbars in front of it (Ok, two spawned), one crowbar at the host's own body (Ok, the "Spawn at" call), the event arirGraff_5 (Ok), a story event
//      (Refused) and a prop the game does not list (UnknownItem): the host's balance and crowbars moved by the Oks
//      and by nothing else, and its watch committed the event, the host answered each as counted, and the client's game mirrors the points, the props and the
//      event.
//   F  the host resets the setting; the client sees it off and its next request is answered Off.
// Lines tagged [CHEAT-DRILL]; the host's PASS or FAIL line is the verdict, the client's own arms print theirs. The host
// leaves the setting at its default. Nothing here ships enabled.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::cheat_drill {

// Every pump tick in a world; a latched read when off. Game thread.
void Tick(coop::net::Session* session);

// The session ended: the drill starts over, a menu it opened is closed, its observer let go.
void OnDisconnect();

}  // namespace coop::dev::cheat_drill

// The pictures of the two games, as rows of text (backlog item 27). The
// games are programs kept in files on the board and carry their pictures
// in their own files (APP_INFO); on the PC, where they are part of the
// system, core/src/gfx/art.cpp makes its bitmaps from the same rows. One
// drawing, two homes.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace gfx {
namespace game_art {

constexpr const char *kSnake16[16] = {
    "............KKKK", "............KWWK", "............KWWK", "............KKKK",
    "...BBBBBBBB.....", "...BSSSSSRBWW...", "...BSBBBBBB.....", "...BSB..........",
    "...BSBBBBBBBBB..", "...BSSSSSSSSSB..", "...BBBBBBBBBSB..", "...........BSB..",
    "..BBBBBBBBBBSB..", "..BSSSSSSSSSSB..", "..BBBBBBBBBBBB..", "................",
};
constexpr app::Ink kSnake16Inks[] = {
    {'K', rgb(4, 0, 0)},
    {'W', rgb(7, 0, 0)},
    {'B', rgb(0, 3, 0)},
    {'S', rgb(1, 5, 0)},
    {'R', rgb(7, 7, 3)},
};

constexpr const char *kSnake32[32] = {
    "..........................K.....", "..........................WW....",
    ".........................BSSW...", "........................WSSSSW..",
    "........................WSSSSW..", ".........................WSSW...",
    "..........................WW....", "..................KKKKKKKK......",
    "......KKKKKKKKKKKKKRRRRRRK......", "......KYRRRRRRRRRRKYYYOOYK......",
    "......KYYKKYYKKYYKKYYYOPYK...S..", "......KYYKKYYKKYYKKYYYYYYKSSS...",
    "......KRYYKKKKKKKKKYYYYYYK...S..", "......KRYYK.......KKKKKKKK......",
    "......KRYYK.....................", "......KRYYK.....................",
    "......KRYYKKKKKKKKKKKKKKKKKKK...", "......KRYYRRRRRRRRRRRRRRRRRYK...",
    "......KRYYKYYKKYYKKYYKKYYKKYK...", "......KYYYKYYKKYYKKYYKKYYKKYK...",
    "......KKKKKKKKKKKKKKKKKKKRYYK...", "........................KRYYK...",
    "........................KRYYK...", "........................KRYYK...",
    "....KKKKKKKKKKKKKKKKKKKKKRYYK...", "....KYRRRRRRRRRRRRRRRRRRRRYYK...",
    "....KYYKKYYKKYYKKYYKKYYKKRYYK...", "....KYYKKYYKKYYKKYYKKYYKKYYYK...",
    "....KKKKKKKKKKKKKKKKKKKKKKKKK...", "................................",
    "................................", "................................",
};
constexpr app::Ink kSnake32Inks[] = {
    {'K', rgb(0, 3, 0)},
    {'W', rgb(4, 0, 0)},
    {'B', rgb(7, 4, 2)},
    {'S', rgb(7, 0, 0)},
    {'R', rgb(4, 7, 1)},
    {'Y', rgb(1, 5, 0)},
    {'O', rgb(7, 7, 3)},
    {'P', rgb(0, 0, 0)},
};

constexpr const char *kCactus16[16] = {
    "......KKK.......", ".....KWWWK......", "......KKKK......", "......KBSK......",
    "......KBSK.KK...", "..KK..KBSK.KSK..", ".KBSK.KBSK.KSK..", ".KBSK.KBSKKKSK..",
    ".KBSKKKBSSSSK...", ".KBSSSSBSKKK....", "..KKKKKBSK......", "....KKKKKKKK....",
    "....KRRRRRRK....", ".....KRRRRK.....", ".....KRRRRK.....", ".....KKKKKK.....",
};
constexpr app::Ink kCactus16Inks[] = {
    {'K', rgb(0, 0, 0)},
    {'W', rgb(7, 3, 2)},
    {'B', rgb(4, 7, 1)},
    {'S', rgb(1, 5, 0)},
    {'R', rgb(7, 4, 0)},
};

constexpr const char *kCactus32[32] = {
    "..............KKK...............", ".............KWWWK..............",
    ".............KWWWK..............", ".............KWWWKB.............",
    "............BSKKKRYB............", "............BSRRRRYB............",
    "............BSRRORYB............", "............BSRRRRYB....BBB.....",
    "....BBB.....BSRRRRYB...BSRYB....", "...BSRYB....BSRORRYB...BSRYB....",
    "...BSRYB....BSRRRRYB...BSRYB....", "...BSRYB....BSRRRRYB...BSRYB....",
    "...BSRYB....BSRRRRYBBBBBRRYB....", "...BSRYB....BSRRORRRRRRRRRYB....",
    "...BSRRBBBBBBSRRRRRRRRRRRYB.....", "...BSRRRRRRRRRRRRRYBBBBBBB......",
    "....BSRRRRRRRRORRRYB............", ".....BBBBBBBBSRRRRYB............",
    "............BSRRRRYB............", "............BSRRRRYB............",
    "............BSRORRYB............", "............BSRRRRYB............",
    ".......BBBBBBBBBBBBBBBBBB.......", ".......BPPPPPPPPPPPPPPPPB.......",
    ".......BBBBBBBBBBBBBBBBBB.......", "........BGPPPPPPPPPPPPDB........",
    "........BGPPPPPPPPPPPPDB........", ".........BGPPPPPPPPPPDB.........",
    ".........BGPPPPPPPPPPDB.........", "..........BGPPPPPPPPDB..........",
    "..........BBBBBBBBBBBB..........", "................................",
};
constexpr app::Ink kCactus32Inks[] = {
    {'K', rgb(4, 0, 0)},
    {'W', rgb(7, 3, 2)},
    {'B', rgb(0, 0, 0)},
    {'S', rgb(4, 7, 1)},
    {'R', rgb(1, 5, 0)},
    {'Y', rgb(0, 3, 0)},
    {'O', rgb(7, 7, 3)},
    {'P', rgb(7, 4, 0)},
    {'G', rgb(6, 4, 1)},
    {'D', rgb(4, 2, 0)},
};

// Chess: the knight of the game's Contour set, drawn small by the same
// script (tools/mockup/chess_pieces.py icon).
constexpr const char *kChess16[16] = {
    "................", "................", "......KK........", ".....KKKKK......",
    ".....KWWWK......", "....KKWWWKK.....", "...KKWWWWWKK....", "...KWWWWWWWK....",
    "...KKKKWWWWK....", "......KWWWWKK...", ".....KKWWWWWK...", ".....KWWWWWKK...",
    "....KKWWWWWK....", "...KKWWWWWWKK...", "....KKKKKKKK....", "................",
};
constexpr const char *kChess32[32] = {
    "................................", "................................",
    "................................", "..............K.................",
    "...........KK.KK................", "...........KKKKK................",
    "...........KWWWKKKK.............", "..........KKWWWWWWKK............",
    "..........KWWWWWWWWK............", ".........KKWWWWWWWWKK...........",
    ".........KWWWWWWWWWWKKK.........", "........KKWWWWWWWWWWWWK.........",
    ".......KKWWWWWWWWWWWWWK.........", ".......KWWWWWWWWWWWWWWKK........",
    "......KKWWWWKKKWWWWWWWWKK.......", "......KWWWWKK.KWWWWWWWWWK.......",
    "......KKWWKK.KKWWWWWWWWWK.......", ".......KKKK.KKWWWWWWWWWWK.......",
    "............KWWWWWWWWWWWK.......", "...........KKWWWWWWWWWWWKK......",
    "...........KWWWWWWWWWWWWK.......", "..........KKWWWWWWWWWWWWK.......",
    "..........KWWWWWWWWWWWWWK.......", ".........KKWWWWWWWWWWWKKK.......",
    ".........KWWWWWWWWWWWWK.........", ".........KWWWWWWWWWWWWK.........",
    ".......KKKWWWWWWWWWWWWKKK.......", ".......KWWWWWWWWWWWWWWWWK.......",
    ".......KKKKKKKKKKKKKKKKKK.......", "................................",
    "................................", "................................",
};
constexpr app::Ink kChessInks[] = {
    {'K', rgb(0, 0, 0)},
    {'W', rgb(7, 7, 3)},
};

} // namespace game_art
} // namespace gfx

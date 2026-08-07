#ifndef SEVEN_SAGES_SELECT_MENU_H
#define SEVEN_SAGES_SELECT_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

struct GameState;
struct FileChooseContext;

// Sage select, reached from the Seven Sages entry on the quest menu. Mirrors the Boss Rush
// menu's split: input here, drawing here, and only the dispatch-table entries live in
// z_file_choose.c.
void FileChoose_UpdateSevenSagesMenu(struct GameState* gameState);
void FileChoose_DrawSevenSagesMenuWindowContents(struct FileChooseContext* fileChooseContext);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_SELECT_MENU_H

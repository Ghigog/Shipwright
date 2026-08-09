#pragma once

#include <ship/window/gui/GuiWindow.h>

class EnrichWorldPlacerWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void DrawElement() override;
    void InitElement() override;
    void UpdateElement() override{};

  private:
    // The palette is picked in two steps - a category, then a prop within it - because one flat
    // list of 100+ entries is not browsable. The category is an index into the filtered list for
    // the room you are standing in, so it is re-clamped every frame rather than trusted.
    int selectedGroup = 0;

    // The prop is remembered by identity, not position, because the list re-sorts underneath it.
    // PlaceableProps orders natives first and "native" means Object_GetIndex finds the object in
    // this room's bank *right now* - so placing a non-native prop loads its object, makes it
    // native, and moves it up into the natives block on the very next frame. An index would then
    // be pointing at whatever slid into its place, which read as the placer stepping to the next
    // prop every time you placed one. -1 means "nothing chosen yet, take the first".
    int selectedActorId = -1;
    int16_t selectedParams = 0;
    int selectedPlacement = -1; // index into EnrichWorld::Placements()
    int paramsOverride = 0;
    bool paramsEdited = false;
    bool snapToGround = true;
};

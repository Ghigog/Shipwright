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
    // list of 100+ entries is not browsable. Both indices are into the *filtered* lists for the
    // room you are standing in, so both are re-clamped every frame rather than trusted.
    int selectedGroup = 0;
    int selectedProp = 0;
    int selectedPlacement = -1; // index into EnrichWorld::Placements()
    int paramsOverride = 0;
    bool paramsEdited = false;
    bool snapToGround = true;
};

#pragma once

#include <ship/window/gui/GuiWindow.h>

class EnrichWorldPlacerWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void DrawElement() override;
    void InitElement() override;
    void UpdateElement() override {};

  private:
    int selectedProp = 0;    // index into the filtered palette
    int selectedPlacement = -1; // index into EnrichWorld::Placements()
    int paramsOverride = 0;
    bool paramsEdited = false;
    bool snapToGround = true;
};

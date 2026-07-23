#pragma once

#include "display/lcd_display.h"

/**
 * @brief Класс GIF-эмоций Otto для дисплея.
 * Расширяет SpiLcdDisplay и добавляет поддержку GIF-эмоций через EmojiCollection.
 */
class OttoEmojiDisplay : public SpiLcdDisplay {
   public:
    /**
     * @brief Конструктор с теми же параметрами, что и у SpiLcdDisplay.
     */
    OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~OttoEmojiDisplay() = default;
    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;

   private:
    void ApplyInstrumentProfile();
    void InitializeOttoEmojis();
    void SetupPreviewImage();
};

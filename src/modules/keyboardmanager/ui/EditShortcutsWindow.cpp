#include "pch.h"
#include "EditShortcutsWindow.h"
#include "ShortcutControl.h"
#include "KeyDropDownControl.h"
#include "XamlBridge.h"
#include <keyboardmanager/common/trace.h>
#include <keyboardmanager/common/KeyboardManagerConstants.h>
#include <common/windows_colors.h>
#include <common/dpi_aware.h>
#include "Styles.h"
#include "Dialog.h"
#include <keyboardmanager/dll/Generated Files/resource.h>
#include <keyboardmanager/common/KeyboardManagerState.h>
#include "common/common.h"
#include "LoadingAndSavingRemappingHelper.h"
extern "C" IMAGE_DOS_HEADER __ImageBase;

using namespace winrt::Windows::Foundation;

LRESULT CALLBACK EditShortcutsWindowProc(HWND, UINT, WPARAM, LPARAM);

// This Hwnd will be the window handler for the Xaml Island: A child window that contains Xaml.
HWND hWndXamlIslandEditShortcutsWindow = nullptr;
// This variable is used to check if window registration has been done to avoid repeated registration leading to an error.
bool isEditShortcutsWindowRegistrationCompleted = false;
// Holds the native window handle of EditShortcuts Window.
HWND hwndEditShortcutsNativeWindow = nullptr;
std::mutex editShortcutsWindowMutex;
// Stores a pointer to the Xaml Bridge object so that it can be accessed from the window procedure
static XamlBridge* xamlBridgePtr = nullptr;

static IAsyncAction OnClickAccept(
    KeyboardManagerState& keyboardManagerState,
    XamlRoot root,
    std::function<void()> ApplyRemappings)
{
    KeyboardManagerHelper::ErrorType isSuccess = LoadingAndSavingRemappingHelper::CheckIfRemappingsAreValid(ShortcutControl::shortcutRemapBuffer);

    if (isSuccess != KeyboardManagerHelper::ErrorType::NoError)
    {
        if (!co_await Dialog::PartialRemappingConfirmationDialog(root, GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_PARTIALCONFIRMATIONDIALOGTITLE)))
        {
            co_return;
        }
    }
    ApplyRemappings();
}

// Function to create the Edit Shortcuts Window
void createEditShortcutsWindow(HINSTANCE hInst, KeyboardManagerState& keyboardManagerState)
{
    // Window Registration
    const wchar_t szWindowClass[] = L"EditShortcutsWindow";

    if (!isEditShortcutsWindowRegistrationCompleted)
    {
        WNDCLASSEX windowClass = {};
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.lpfnWndProc = EditShortcutsWindowProc;
        windowClass.hInstance = hInst;
        windowClass.lpszClassName = szWindowClass;
        windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW);
        windowClass.hIcon = (HICON)LoadImageW(
            windowClass.hInstance,
            MAKEINTRESOURCE(IDS_KEYBOARDMANAGER_ICON),
            IMAGE_ICON,
            48,
            48,
            LR_DEFAULTCOLOR);
        if (RegisterClassEx(&windowClass) == NULL)
        {
            MessageBox(NULL, GET_RESOURCE_STRING(IDS_REGISTERCLASSFAILED_ERRORMESSAGE).c_str(), GET_RESOURCE_STRING(IDS_REGISTERCLASSFAILED_ERRORTITLE).c_str(), NULL);
            return;
        }

        isEditShortcutsWindowRegistrationCompleted = true;
    }

    // Find center screen coordinates
    RECT desktopRect;
    GetClientRect(GetDesktopWindow(), &desktopRect);
    // Calculate DPI dependent window size
    int windowWidth = KeyboardManagerConstants::DefaultEditShortcutsWindowWidth;
    int windowHeight = KeyboardManagerConstants::DefaultEditShortcutsWindowHeight;
    DPIAware::Convert(nullptr, windowWidth, windowHeight);

    // Window Creation
    HWND _hWndEditShortcutsWindow = CreateWindow(
        szWindowClass,
        GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_WINDOWNAME).c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX,
        (desktopRect.right / 2) - (windowWidth / 2),
        (desktopRect.bottom / 2) - (windowHeight / 2),
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInst,
        NULL);
    if (_hWndEditShortcutsWindow == NULL)
    {
        MessageBox(NULL, GET_RESOURCE_STRING(IDS_CREATEWINDOWFAILED_ERRORMESSAGE).c_str(), GET_RESOURCE_STRING(IDS_CREATEWINDOWFAILED_ERRORTITLE).c_str(), NULL);
        return;
    }
    // Ensures the window is in foreground on first startup. If this is not done, the window appears behind because the thread is not on the foreground.
    if (_hWndEditShortcutsWindow)
    {
        SetForegroundWindow(_hWndEditShortcutsWindow);
    }

    // Store the newly created Edit Shortcuts window's handle.
    std::unique_lock<std::mutex> hwndLock(editShortcutsWindowMutex);
    hwndEditShortcutsNativeWindow = _hWndEditShortcutsWindow;
    hwndLock.unlock();

    // Create the xaml bridge object
    XamlBridge xamlBridge(_hWndEditShortcutsWindow);
    // DesktopSource needs to be declared before the RelativePanel xamlContainer object to avoid errors
    winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource desktopSource;
    // Create the desktop window xaml source object and set its content
    hWndXamlIslandEditShortcutsWindow = xamlBridge.InitDesktopWindowsXamlSource(desktopSource);

    // Set the pointer to the xaml bridge object
    xamlBridgePtr = &xamlBridge;

    // Header for the window
    Windows::UI::Xaml::Controls::RelativePanel header;
    header.Margin({ 10, 10, 10, 30 });

    // Header text
    TextBlock headerText;
    headerText.Text(GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_WINDOWNAME));
    headerText.FontSize(30);
    headerText.Margin({ 0, 0, 0, 0 });
    header.SetAlignLeftWithPanel(headerText, true);

    // Cancel button
    Button cancelButton;
    cancelButton.Content(winrt::box_value(GET_RESOURCE_STRING(IDS_CANCEL_BUTTON)));
    cancelButton.Margin({ 10, 0, 0, 0 });
    cancelButton.Click([&](winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
        // Close the window since settings do not need to be saved
        PostMessage(_hWndEditShortcutsWindow, WM_CLOSE, 0, 0);
    });

    //  Text block for information about remap key section.
    TextBlock shortcutRemapInfoHeader;
    shortcutRemapInfoHeader.Text(GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_INFO));
    shortcutRemapInfoHeader.Margin({ 10, 0, 0, 10 });
    shortcutRemapInfoHeader.FontWeight(Text::FontWeights::SemiBold());
    shortcutRemapInfoHeader.TextWrapping(TextWrapping::Wrap);

    TextBlock shortcutRemapInfoExample;
    shortcutRemapInfoExample.Text(GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_INFOEXAMPLE));
    shortcutRemapInfoExample.Margin({ 10, 0, 0, 20 });
    shortcutRemapInfoExample.FontStyle(Text::FontStyle::Italic);
    shortcutRemapInfoExample.TextWrapping(TextWrapping::Wrap);

    // Table to display the shortcuts
    Windows::UI::Xaml::Controls::Grid shortcutTable;
    Grid keyRemapTable;
    ColumnDefinition originalColumn;
    originalColumn.MinWidth(3 * KeyboardManagerConstants::ShortcutTableDropDownWidth + 2 * KeyboardManagerConstants::ShortcutTableDropDownSpacing);
    originalColumn.MaxWidth(3 * KeyboardManagerConstants::ShortcutTableDropDownWidth + 2 * KeyboardManagerConstants::ShortcutTableDropDownSpacing);
    ColumnDefinition arrowColumn;
    arrowColumn.MinWidth(KeyboardManagerConstants::TableArrowColWidth);
    ColumnDefinition newColumn;
    newColumn.MinWidth(3 * KeyboardManagerConstants::ShortcutTableDropDownWidth + 2 * KeyboardManagerConstants::ShortcutTableDropDownSpacing);
    newColumn.MaxWidth(3 * KeyboardManagerConstants::ShortcutTableDropDownWidth + 2 * KeyboardManagerConstants::ShortcutTableDropDownSpacing);
    ColumnDefinition targetAppColumn;
    targetAppColumn.MinWidth(KeyboardManagerConstants::TableTargetAppColWidth);
    ColumnDefinition removeColumn;
    removeColumn.MinWidth(KeyboardManagerConstants::TableRemoveColWidth);
    shortcutTable.Margin({ 10, 10, 10, 20 });
    shortcutTable.HorizontalAlignment(HorizontalAlignment::Stretch);
    shortcutTable.ColumnDefinitions().Append(originalColumn);
    shortcutTable.ColumnDefinitions().Append(arrowColumn);
    shortcutTable.ColumnDefinitions().Append(newColumn);
    shortcutTable.ColumnDefinitions().Append(targetAppColumn);
    shortcutTable.ColumnDefinitions().Append(removeColumn);
    shortcutTable.RowDefinitions().Append(RowDefinition());
    shortcutTable.MinWidth(KeyboardManagerConstants::EditShortcutsTableMinWidth);

    // First header textblock in the header row of the shortcut table
    TextBlock originalShortcutHeader;
    originalShortcutHeader.Text(GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_SOURCEHEADER));
    originalShortcutHeader.FontWeight(Text::FontWeights::Bold());
    originalShortcutHeader.Margin({ 0, 0, 0, 10 });

    // Second header textblock in the header row of the shortcut table
    TextBlock newShortcutHeader;
    newShortcutHeader.Text(GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_TARGETHEADER));
    newShortcutHeader.FontWeight(Text::FontWeights::Bold());
    newShortcutHeader.Margin({ 0, 0, 0, 10 });

    // Third header textblock in the header row of the shortcut table
    TextBlock targetAppHeader;
    targetAppHeader.Text(GET_RESOURCE_STRING(IDS_EDITSHORTCUTS_TARGETAPPHEADER));
    targetAppHeader.Width(KeyboardManagerConstants::ShortcutTableDropDownWidth);
    targetAppHeader.FontWeight(Text::FontWeights::Bold());
    targetAppHeader.Margin({ 0, 0, 0, 10 });
    targetAppHeader.HorizontalAlignment(HorizontalAlignment::Center);

    shortcutTable.SetColumn(originalShortcutHeader, KeyboardManagerConstants::ShortcutTableOriginalColIndex);
    shortcutTable.SetRow(originalShortcutHeader, 0);
    shortcutTable.SetColumn(newShortcutHeader, KeyboardManagerConstants::ShortcutTableNewColIndex);
    shortcutTable.SetRow(newShortcutHeader, 0);
    shortcutTable.SetColumn(targetAppHeader, KeyboardManagerConstants::ShortcutTableTargetAppColIndex);
    shortcutTable.SetRow(targetAppHeader, 0);

    shortcutTable.Children().Append(originalShortcutHeader);
    shortcutTable.Children().Append(newShortcutHeader);
    shortcutTable.Children().Append(targetAppHeader);

    // Store handle of edit shortcuts window
    ShortcutControl::EditShortcutsWindowHandle = _hWndEditShortcutsWindow;
    // Store keyboard manager state
    ShortcutControl::keyboardManagerState = &keyboardManagerState;
    KeyDropDownControl::keyboardManagerState = &keyboardManagerState;
    // Clear the shortcut remap buffer
    ShortcutControl::shortcutRemapBuffer.clear();
    // Vector to store dynamically allocated control objects to avoid early destruction
    std::vector<std::vector<std::unique_ptr<ShortcutControl>>> keyboardRemapControlObjects;

    // Set keyboard manager UI state so that shortcut remaps are not applied while on this window
    keyboardManagerState.SetUIState(KeyboardManagerUIState::EditShortcutsWindowActivated, _hWndEditShortcutsWindow);

    // Load existing os level shortcuts into UI
    // Create copy of the remaps to avoid concurrent access
    ShortcutRemapTable osLevelShortcutReMapCopy = keyboardManagerState.osLevelShortcutReMap;

    for (const auto& it : osLevelShortcutReMapCopy)
    {
        ShortcutControl::AddNewShortcutControlRow(shortcutTable, keyboardRemapControlObjects, it.first, it.second.targetShortcut);
    }

    // Load existing app-specific shortcuts into UI
    // Create copy of the remaps to avoid concurrent access
    AppSpecificShortcutRemapTable appSpecificShortcutReMapCopy = keyboardManagerState.appSpecificShortcutReMap;

    // Iterate through all the apps
    for (const auto& itApp : appSpecificShortcutReMapCopy)
    {
        // Iterate through shortcuts for each app
        for (const auto& itShortcut : itApp.second)
        {
            ShortcutControl::AddNewShortcutControlRow(shortcutTable, keyboardRemapControlObjects, itShortcut.first, itShortcut.second.targetShortcut, itApp.first);
        }
    }

    // Apply button
    Button applyButton;
    applyButton.Content(winrt::box_value(GET_RESOURCE_STRING(IDS_OK_BUTTON)));
    applyButton.Style(AccentButtonStyle());
    applyButton.MinWidth(KeyboardManagerConstants::HeaderButtonWidth);
    cancelButton.MinWidth(KeyboardManagerConstants::HeaderButtonWidth);
    header.SetAlignRightWithPanel(cancelButton, true);
    header.SetLeftOf(applyButton, cancelButton);

    auto ApplyRemappings = [&keyboardManagerState, _hWndEditShortcutsWindow]() {
        // Disable the remappings while the remapping table is updated
        keyboardManagerState.RemappingsDisabledWrapper(
            [&keyboardManagerState]() {
                LoadingAndSavingRemappingHelper::ApplyShortcutRemappings(keyboardManagerState, ShortcutControl::shortcutRemapBuffer, true);
                // Save the updated key remaps to file.
                bool saveResult = keyboardManagerState.SaveConfigToFile();
            });
        PostMessage(_hWndEditShortcutsWindow, WM_CLOSE, 0, 0);
    };

    applyButton.Click([&keyboardManagerState, applyButton, ApplyRemappings](winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
        OnClickAccept(keyboardManagerState, applyButton.XamlRoot(), ApplyRemappings);
    });

    header.Children().Append(headerText);
    header.Children().Append(applyButton);
    header.Children().Append(cancelButton);

    ScrollViewer scrollViewer;
    scrollViewer.VerticalScrollMode(ScrollMode::Enabled);
    scrollViewer.HorizontalScrollMode(ScrollMode::Enabled);
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);

    // Add shortcut button
    Windows::UI::Xaml::Controls::Button addShortcut;
    FontIcon plusSymbol;
    plusSymbol.FontFamily(Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
    plusSymbol.Glyph(L"\xE109");
    addShortcut.Content(plusSymbol);
    addShortcut.Margin({ 10, 0, 0, 25 });
    addShortcut.Click([&](winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
        ShortcutControl::AddNewShortcutControlRow(shortcutTable, keyboardRemapControlObjects);
        // Whenever a remap is added move to the bottom of the screen
        scrollViewer.ChangeView(nullptr, scrollViewer.ScrollableHeight(), nullptr);
    });
    // Set accessible name for the add shortcut button
    addShortcut.SetValue(Automation::AutomationProperties::NameProperty(), box_value(GET_RESOURCE_STRING(IDS_ADD_SHORTCUT_BUTTON)));

    // Header and example text at the top of the window
    StackPanel helperText;
    helperText.Children().Append(shortcutRemapInfoHeader);
    helperText.Children().Append(shortcutRemapInfoExample);

    // Remapping table
    StackPanel mappingsPanel;
    mappingsPanel.Children().Append(shortcutTable);
    mappingsPanel.Children().Append(addShortcut);

    // Remapping table should be scrollable
    scrollViewer.Content(mappingsPanel);

    RelativePanel xamlContainer;
    xamlContainer.SetBelow(helperText, header);
    xamlContainer.SetBelow(scrollViewer, helperText);
    xamlContainer.SetAlignLeftWithPanel(header, true);
    xamlContainer.SetAlignRightWithPanel(header, true);
    xamlContainer.SetAlignLeftWithPanel(helperText, true);
    xamlContainer.SetAlignRightWithPanel(helperText, true);
    xamlContainer.SetAlignLeftWithPanel(scrollViewer, true);
    xamlContainer.SetAlignRightWithPanel(scrollViewer, true);
    xamlContainer.Children().Append(header);
    xamlContainer.Children().Append(helperText);
    xamlContainer.Children().Append(scrollViewer);
    xamlContainer.UpdateLayout();

    desktopSource.Content(xamlContainer);

    ////End XAML Island section
    if (_hWndEditShortcutsWindow)
    {
        ShowWindow(_hWndEditShortcutsWindow, SW_SHOW);
        UpdateWindow(_hWndEditShortcutsWindow);
    }

    // Message loop:
    xamlBridge.MessageLoop();

    // Reset pointers to nullptr
    xamlBridgePtr = nullptr;
    hWndXamlIslandEditShortcutsWindow = nullptr;
    hwndLock.lock();
    hwndEditShortcutsNativeWindow = nullptr;
    keyboardManagerState.ResetUIState();
    keyboardManagerState.ClearRegisteredKeyDelays();

    // Cannot be done in WM_DESTROY because that causes crashes due to fatal app exit
    xamlBridge.ClearXamlIslands();
}

LRESULT CALLBACK EditShortcutsWindowProc(HWND hWnd, UINT messageCode, WPARAM wParam, LPARAM lParam)
{
    RECT rcClient;
    switch (messageCode)
    {
    // Resize the XAML window whenever the parent window is painted or resized
    case WM_PAINT:
    case WM_SIZE:
    {
        GetClientRect(hWnd, &rcClient);
        SetWindowPos(hWndXamlIslandEditShortcutsWindow, 0, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom, SWP_SHOWWINDOW);
    }
    break;
    // To avoid UI elements overlapping on making the window smaller enforce a minimum window size
    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        int minWidth = KeyboardManagerConstants::MinimumEditShortcutsWindowWidth;
        int minHeight = KeyboardManagerConstants::MinimumEditShortcutsWindowHeight;
        DPIAware::Convert(nullptr, minWidth, minHeight);
        lpMMI->ptMinTrackSize.x = minWidth;
        lpMMI->ptMinTrackSize.y = minHeight;
    }
    break;
    default:
        // If the Xaml Bridge object exists, then use it's message handler to handle keyboard focus operations
        if (xamlBridgePtr != nullptr)
        {
            return xamlBridgePtr->MessageHandler(messageCode, wParam, lParam);
        }
        else if (messageCode == WM_NCDESTROY)
        {
            PostQuitMessage(0);
            break;
        }
        return DefWindowProc(hWnd, messageCode, wParam, lParam);
        break;
    }

    return 0;
}

// Function to check if there is already a window active if yes bring to foreground
bool CheckEditShortcutsWindowActive()
{
    bool result = false;
    std::unique_lock<std::mutex> hwndLock(editShortcutsWindowMutex);
    if (hwndEditShortcutsNativeWindow != nullptr)
    {
        // Check if the window is minimized if yes then restore the window.
        if (IsIconic(hwndEditShortcutsNativeWindow))
        {
            ShowWindow(hwndEditShortcutsNativeWindow, SW_RESTORE);
        }

        // If there is an already existing window no need to create a new open bring it on foreground.
        SetForegroundWindow(hwndEditShortcutsNativeWindow);
        result = true;
    }

    return result;
}

// Function to close any active Edit Shortcuts window
void CloseActiveEditShortcutsWindow()
{
    std::unique_lock<std::mutex> hwndLock(editShortcutsWindowMutex);
    if (hwndEditShortcutsNativeWindow != nullptr)
    {
        PostMessage(hwndEditShortcutsNativeWindow, WM_CLOSE, 0, 0);
    }
}

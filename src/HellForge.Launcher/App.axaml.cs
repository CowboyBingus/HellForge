using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Layout;
using Avalonia.Markup.Xaml;
using Avalonia.Media;

namespace HellForge.Launcher;

public partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var paths = LauncherPaths.Find();
            desktop.MainWindow = paths != null ? new MainWindow(paths) : new Window
            {
                Title = "HellForge Launcher",
                Width = 480,
                Height = 150,
                WindowStartupLocation = WindowStartupLocation.CenterScreen,
                Content = new TextBlock
                {
                    Text = "Run the launcher from a HellForge installation or the HellForge repository.",
                    Margin = new Thickness(24),
                    TextWrapping = TextWrapping.Wrap,
                    VerticalAlignment = VerticalAlignment.Center
                }
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}

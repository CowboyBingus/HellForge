// From scripts/HostWindowText.cs, as sessions compiled it.
#nullable disable
#pragma warning disable
// Read only the test process's window/control text, with bounded cross-process calls.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public sealed class HostWindowEntry { public string Title;public List<string> Controls=new List<string>(); }
public static class HostWindowText {
    delegate bool EnumCallback(IntPtr window,IntPtr value);
    [DllImport("user32.dll")]static extern bool EnumWindows(EnumCallback callback,IntPtr value);
    [DllImport("user32.dll")]static extern bool EnumChildWindows(IntPtr window,EnumCallback callback,IntPtr value);
    [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowTextW(IntPtr window,StringBuilder value,int max);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern IntPtr SendMessageTimeoutW(IntPtr window,uint message,UIntPtr wparam,StringBuilder lparam,uint flags,uint timeout,out UIntPtr result);
    public static List<HostWindowEntry> Read(int processId) {
        var output=new List<HostWindowEntry>();
        EnumWindows(delegate(IntPtr window,IntPtr unused){
            uint pid;GetWindowThreadProcessId(window,out pid);if(pid!=(uint)processId||output.Count>=8)return true;
            var title=new StringBuilder(2048);GetWindowTextW(window,title,title.Capacity);
            var entry=new HostWindowEntry{Title=title.ToString()};int controls=0;
            EnumChildWindows(window,delegate(IntPtr child,IntPtr ignored){
                if(++controls>16)return false;
                var text=new StringBuilder(2048);UIntPtr result;
                if(SendMessageTimeoutW(child,13,new UIntPtr((uint)text.Capacity),text,2,50,out result)!=IntPtr.Zero&&text.Length>0)entry.Controls.Add(text.ToString());
                return true;
            },IntPtr.Zero);
            output.Add(entry);return true;
        },IntPtr.Zero);
        return output;
    }
}

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Windows.Automation;

// wubua.cs — WuBuDesk UI Automation controller (Windows 11-compliant desktop control).
// SPDX-License-Identifier: WaefreBeorn-UMV3
// Finds controls by NAME and invokes them (the way Windows 11 expects), instead of
// pixel guessing. Must run at the SAME integrity level as the target window
// (elevated target => run this elevated, or UIPI blocks it).
//
// Build:  csc /nologo /r:UIAutomationClient.dll /r:UIAutomationTypes.dll wubua.cs
// Usage:
//   wubua list                       enumerate top-level windows (title, hwnd)
//   wubua find <title> <control>     find a control by name under a window
//   wubua click <title> <control>    find + Invoke a button by name
//   wubua text <title> <control>     print a control's Name (for dialogs)
//   wubua dump <title>               dump all descendant control names
class Wubua
{
    static int Fail(string m) { Console.Error.WriteLine(m); return 1; }

    static AutomationElement FindWindow(string titleSub)
    {
        var root = AutomationElement.RootElement;
        var cond = new PropertyCondition(AutomationElement.NameProperty, titleSub);
        var all = root.FindAll(TreeScope.Children, Condition.TrueCondition);
        foreach (AutomationElement w in all)
        {
            try { if (w.Current.Name.IndexOf(titleSub, StringComparison.OrdinalIgnoreCase) >= 0) return w; }
            catch (Exception) { }
        }
        return null;
    }

    static AutomationElement FindControl(AutomationElement win, string name)
    {
        var all = win.FindAll(TreeScope.Descendants, Condition.TrueCondition);
        foreach (AutomationElement el in all)
        {
            try { if (el.Current.Name.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0) return el; }
            catch (Exception) { }
        }
        return null;
    }

    static IEnumerable<AutomationElement> AllControls(AutomationElement win)
    {
        var all = win.FindAll(TreeScope.Descendants, Condition.TrueCondition);
        foreach (AutomationElement el in all) yield return el;
    }

    static int CmdList()
    {
        var root = AutomationElement.RootElement;
        foreach (AutomationElement w in root.FindAll(TreeScope.Children, Condition.TrueCondition))
        {
            try
            {
                string n = w.Current.Name;
                if (!string.IsNullOrWhiteSpace(n))
                    Console.WriteLine("  [{0}] {1}", w.Current.NativeWindowHandle, n);
            }
            catch (Exception) { }
        }
        return 0;
    }

    static int CmdFind(string title, string ctl)
    {
        var win = FindWindow(title);
        if (win == null) return Fail("no window matching '" + title + "'");
        var el = FindControl(win, ctl);
        if (el == null) return Fail("no control '" + ctl + "' in '" + title + "'");
        Console.WriteLine("found: {0} (type {1})", el.Current.Name, el.Current.ControlType.ProgrammaticName);
        return 0;
    }

    static int CmdClick(string title, string ctl)
    {
        var win = FindWindow(title);
        if (win == null) return Fail("no window matching '" + title + "'");
        var el = FindControl(win, ctl);
        if (el == null) return Fail("no control '" + ctl + "' in '" + title + "'");
        var ip = el.GetCurrentPattern(InvokePattern.Pattern) as InvokePattern;
        if (ip == null) return Fail("control not invokable: " + el.Current.Name);
        ip.Invoke();
        Console.WriteLine("invoked: {0}", el.Current.Name);
        return 0;
    }

    static int CmdDump(string title)
    {
        var win = FindWindow(title);
        if (win == null) return Fail("no window matching '" + title + "'");
        int n = 0;
        foreach (AutomationElement el in AllControls(win))
        {
            try
            {
                string nm = el.Current.Name;
                if (!string.IsNullOrWhiteSpace(nm) && nm.Length < 200)
                    Console.WriteLine("  [{0}] {1}", el.Current.ControlType.ProgrammaticName, nm);
                if (++n > 300) break;
            }
            catch (Exception) { }
        }
        return 0;
    }

    static int Main(string[] args)
    {
        if (args.Length < 1) return Fail("wubua: list | find <t> <c> | click <t> <c> | dump <t>");
        try
        {
            switch (args[0])
            {
                case "list": return CmdList();
                case "find": return args.Length >= 3 ? CmdFind(args[1], args[2]) : Fail("find needs <title> <control>");
                case "click": return args.Length >= 3 ? CmdClick(args[1], args[2]) : Fail("click needs <title> <control>");
                case "dump": return args.Length >= 2 ? CmdDump(args[1]) : Fail("dump needs <title>");
                default: return Fail("unknown cmd " + args[0]);
            }
        }
        catch (Exception e) { return Fail("error: " + e.Message); }
    }
}

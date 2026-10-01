using System;
using System.Net.Mail;
using System.Runtime.InteropServices;

public static unsafe class MailAddressBridge
{
    [UnmanagedCallersOnly(EntryPoint = "parse_email_address")]
    public static int ParseEmailAddress(
        char* rawInput,
        char** outDisplayName,
        char** outUser,
        char** outHost,
        char** outAddress)
    {
        if (rawInput == null) return -1;

        try
        {
            string input = new string(rawInput);
            var mail = new MailAddress(input);

            if (outDisplayName != null)
                *outDisplayName = (char*)Marshal.StringToCoTaskMemUni(mail.DisplayName);

            if (outUser != null)
                *outUser = (char*)Marshal.StringToCoTaskMemUni(mail.User);

            if (outHost != null)
                *outHost = (char*)Marshal.StringToCoTaskMemUni(mail.Host);

            if (outAddress != null)
                *outAddress = (char*)Marshal.StringToCoTaskMemUni(mail.Address);

            return 0; // Success
        }
        catch (FormatException)
        {
            return 1; // Invalid RFC format
        }
        catch (Exception)
        {
            return -1; // General failure
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "free_email_string")]
    public static void FreeEmailString(char* ptr)
    {
        if (ptr != null)
        {
            Marshal.FreeCoTaskMem((IntPtr)ptr);
        }
    }
}
package com.redhat.systemtaplanguageserver;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.lsp4e.server.ProcessStreamConnectionProvider;
import org.eclipse.lsp4e.server.StreamConnectionProvider;

public class StapStreamConnectionProvider extends ProcessStreamConnectionProvider implements StreamConnectionProvider{

    public StapStreamConnectionProvider() {
        List<String> commands = new ArrayList<>();
        commands.add("stap");
        commands.add("--language-server"); 
        setCommands(commands);
        setWorkingDirectory(System.getProperty("user.dir"));
    }
}
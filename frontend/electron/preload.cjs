const { contextBridge } = require('electron')

contextBridge.exposeInMainWorld('guardianBridge', {})

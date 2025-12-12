const { app, BrowserWindow } = require('electron')
const path = require('node:path')

const isDev = process.argv.includes('--dev') || process.env.NODE_ENV === 'development'
const devPort = process.env.VITE_DEV_SERVER_PORT || '5174'

const createWindow = () => {
  const win = new BrowserWindow({
    width: 1280,
    height: 900,
    backgroundColor: '#f6f7fb',
    webPreferences: {
      preload: path.resolve(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      webviewTag: true,
    },
  })

  if (isDev) {
    win.loadURL(`http://localhost:${devPort}`)
  } else {
    win.loadFile(path.resolve(__dirname, '../dist/index.html'))
  }
}

app.whenReady().then(() => {
  createWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})

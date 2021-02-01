#!/usr/bin/env node
'use strict'

const { spawn } = require('child_process')

function logOutput (process, name) {
  process.stdout.on('data', (data) => console.log(`(${name}) stdout: ${data}`))
  process.stderr.on('data', (data) => console.log(`(${name}) stderr: ${data}`))
}

async function startBroker () {
  return new Promise((resolve, reject) => {
    const broker = spawn('pstore-brokerd', ['--http-port=0', '--announce-http-port'])
    logOutput(broker, 'broker')

    const removeListeners = () => {
      broker.stdout.off('data', dataHandler)
      broker.off('exit', exitHandler)
      broker.off('error', errorHandler)
    }
    const dataHandler = (data) => {
      const match = /HTTP listening on port ([0-9]+)/.exec(data)
      if (match !== null) {
        const port = parseInt(match[1], 10)
        console.log(`(broker) port: ${port}`)
        broker.stdout.off('data', dataHandler)
        resolve([broker, port])
      }
    }
    const errorHandler = (err) => {
      console.error(`(broker) ${err}`)
      removeListeners()
      reject(err)
    }
    const exitHandler = (code) => errorHandler(new Error(`exited ${code}`))
    broker.stdout.on('data', dataHandler)
    broker.on('exit', exitHandler).on('error', errorHandler)
  })
}

async function startHttpClient (port) {
  return new Promise((resolve, reject) => {
    let output = ''
    const record = (data) => { output += data }

    const client = spawn('./build/http-client', ['localhost', port, '/index.html'])
    logOutput(client, 'http-client')
    client.stdout.on('data', record)
    client.stderr.on('data', record)
    client.on('exit', (code) => {
      console.log(`(http-client) child process exited with code ${code}`)
      resolve([output, code])
    })
    client.on('error', (err) => {
      console.error(`(http-client) error: ${err}`)
      reject(err)
    })
  })
}

async function stopBroker (broker) {
  console.log('stopping the broker')
  return new Promise((resolve, reject) => {
    broker.on('exit', (code) => {
      resolve(code)
    })
    broker.on('error', (err) => {
      console.error(`(broker) error: ${err}`)
      reject(err)
    })
    broker.kill()
  })
}

async function main () {
  const [broker, port] = await startBroker()
  const [response, clientExitCode] = await startHttpClient(port)
  const brokerExitCode = await stopBroker(broker)
  console.log(`(broker) exit code ${brokerExitCode}`)

  if (clientExitCode !== 0) {
    throw new Error(`http-client exit code ${clientExitCode}`)
  }
  if ([/<!DOCTYPE html>/, /<p>Hello from the pstore HTTP server!<\/p>/, /<\/html>/].every((value) => value.exec(response))) {
    throw new Error('Required content missing from response ')
  }
}

if (require.main === module) {
  try {
    main()
  } catch (err) {
    console.error(err)
    process.exit(1)
  }
}

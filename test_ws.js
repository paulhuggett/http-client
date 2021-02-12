#!/usr/bin/env node
'use strict'

const { spawn } = require('child_process')

function logOutput (process, name) {
  process.stdout.on('data', (data) => console.log(`(${name}) stdout: ${data}`))
  process.stderr.on('data', (data) => console.log(`(${name}) stderr: ${data}`))
}

const timeout = 2 * 60 * 1000

async function startWSClient (port) {
  return new Promise((resolve, reject) => {
    const client = spawn('./build/ws', ['localhost', port, '/index.html'])
    logOutput(client, 'ws')

    let output = ''
    const record = (data) => { output += data }
    client.stdout.on('data', record)
    client.stderr.on('data', record)
    client
      .on('exit', (code) => {
        console.log(`(ws) child process exited with code ${code}`)
        resolve([output, code])
      })
      .on('error', (err) => {
        console.error(`(ws) error: ${err}`)
        reject(err)
      })
  })
}

function startBroker () {
  let exitCode
  const broker = spawn('pstore-brokerd', ['--http-port=0', '--announce-http-port'])
  logOutput(broker, 'broker')

  const errorHandler = (reject, err) => {
    console.error(`(broker) ${err}`)
    reject(err)
  }

  const timerId = setTimeout(() => {
    console.error('(broker) timeout')
    stopBroker().then(() => { throw new Error('test timeout') })
  }, timeout)

  const readyExitHandler = function (resolve, reject, code) {
    exitCode = code
    clearTimeout(timerId)
    errorHandler(reject, new Error(`exited ${code}`))
  }
  const stopBroker = async function () {
    if (exitCode !== undefined) {
      return Promise.resolved(exitCode)
    }
    return new Promise((resolve, reject) => {
      // Up until now, the broker exiting would be an error. It's not something
      // that we expect or want to happen. However, once we've sent the kill
      // signal to the process this becomes the normal flow of events. Replace
      // the 'exit' event handler to reflect this.
      broker.removeAllListeners('exit')
      broker.on('exit', (code) => {
        exitCode = code
        clearTimeout(timerId)
        resolve(code)
      })
      // We must also reset the error handler so that if it rejects this promise.
      broker.removeAllListeners('error')
      broker.on('error', (err) => errorHandler(reject, err))
      // Tell the broker to exit.
      broker.kill()
    })
  }

  return {
    // Returns a promise which will be resolved when the broker's HTTP server
    // is listening.
    ready: async function () {
      return new Promise((resolve, reject) => {
        const dataHandler = (data) => {
          const match = /HTTP listening on port ([0-9]+)/.exec(data)
          if (match !== null) {
            const port = parseInt(match[1], 10)
            console.log(`(broker) port: ${port}`)
            broker.stdout.off('data', dataHandler)
            resolve(port)
          }
        }
        broker.stdout.on('data', dataHandler)
        broker
          .on('exit', (code) => readyExitHandler(resolve, reject, code))
          .on('error', (err) => errorHandler(reject, err))
      })
    },
    // Returns a promise which is resolved with the broker's exit code.
    stop: stopBroker
  }
}

async function main () {
  try {
    const broker = startBroker()
    const port = await broker.ready()
    console.log(`port is ${port}`)
    // Run the ws tool and get its output.
    const [response, clientExitCode] = await startWSClient(port)
    // Tell the broker to exit and wait for it to do so.
    const brokerExitCode = await broker.stop()
    console.log(`broker exit code=${brokerExitCode}`)
    // Check the results.
    if (clientExitCode !== 0) {
      throw new Error(`ws exit code ${clientExitCode}`)
    }
  } catch (err) {
    console.error(err)
    return false
  }
  return true
}

if (require.main === module) {
  if (!main()) {
    process.exit(1)
  }
}

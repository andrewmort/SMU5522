document.addEventListener('DOMContentLoaded', (event) => {
  ///////////////////////////////////////////////////////////
  //  Websocket Connection
  ///////////////////////////////////////////////////////////
  let connection;
  let reconnect_delay = 5000; // 5 seconds delay before trying to reconnect
  let reconnect_tries = 10; // Maximum number of reconnection attempts
  let reconnect_num = 0;      // Number of attempted reconnections

  function connect_websocket() {
    // Create WebSocket connection
    connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);

    connection.onopen = function () {
      connection.send('Connect ' + new Date());
      console.log('WebSocket connected');
      reconnect_num = 0;
    };

    connection.onerror = function (error) {
      console.log('WebSocket Error ', error);
    };

    connection.onclose = function(){
      console.log('WebSocket connection closed');
      if (reconnect_num < reconnect_tries) {
        reconnect_num++;
        console.log(`Attempting to reconnect in ${reconnect_delay / 1000} seconds (Retry ${reconnect_num}/${reconnect_tries})`);
        setTimeout(connect_websocket, reconnect_delay); // Attempt to reconnect
      } else {
        console.log('Max retries reached. WebSocket will not reconnect.');
      }
    };

    // Handle incoming messages from WebSocket
    connection.onmessage = function(event) {
      console.log('Server: ', event.data);
      const data = JSON.parse(event.data);
      if (data.type === "smu") {
        updateChannel(data);
      }
    };
  }

  // Connect websocket initially
  connect_websocket();

  /***********************************************
   * Range Modal
   ***********************************************/

  // Get modal and close button
  const modal = document.getElementById("currentRangeModal");
  const closeButton = document.querySelector(".close-button");

  // Store the current channel being updated
  let currentChannel = null;

  // Function to dynamically attach event listeners
  function attachRangeButtonEvents(channelId) {
      const rangeButton = document.querySelector(`#channel-${channelId} .range-button`);

      // Open modal when the range button is clicked
      rangeButton.addEventListener("click", function () {
          currentChannel = channelId; // Track which channel is being updated
          modal.style.display = "flex"; // Show modal
      });
  }

  // Close modal when close button is clicked
  closeButton.addEventListener("click", function () {
      modal.style.display = "none"; // Hide modal
  });

  // Close modal when clicking outside of the modal content
  window.addEventListener("click", function (event) {
      if (event.target === modal) {
          modal.style.display = "none"; // Hide modal
      }
  });

  // Handle selection of current range
  document.getElementById("currentRangeOptions").addEventListener("click", function (event) {
      if (event.target.tagName === "LI") {
          const selectedValue = event.target.getAttribute("data-value");

          // Update the corresponding channel's range value
          const channelElement = document.querySelector(`#channel-${currentChannel}`);
          const rangeButton = channelElement.querySelector(".range-button");
          rangeButton.textContent = selectedValue;

          modal.style.display = "none"; // Hide modal
      }
  });

  /***********************************************
   * Keypad Modal
   ***********************************************/
 
  let currentValue = "0";
  let currentSign = "+";
  let currentUnit = "m";

  const inputDisplay = document.getElementById("numericInput");
  const signDisplay = document.getElementById("sign");
  const unitDisplay = document.getElementById("unit");

  // Function to update display
  function updateDisplay() {
    inputDisplay.value = currentValue;
    signDisplay.textContent = currentSign;
    unitDisplay.textContent = currentUnit;
  }

  function attachKeypadEvents(channelId) {
      const keypadButton = document.querySelector(`#channel-${channelId} .vbutton`);

      // Open modal when the range button is clicked
      keypadButton.addEventListener("click", function () {
          currentChannel = channelId; // Track which channel is being updated
          openNumericInputModal("10", "", "+");
      });
  }

  // Handle numeric keys
  document.querySelectorAll(".key[data-key]").forEach((key) => {
    key.addEventListener("click", () => {
      if (currentValue === "0" && key.dataset.key !== ".") {
        currentValue = key.dataset.key;
      } else {
        currentValue += key.dataset.key;
      }
      updateDisplay();
    });
  });

  // Handle backspace
  document.getElementById("backspace").addEventListener("click", () => {
    currentValue = currentValue.slice(0, -1) || "0";
    updateDisplay();
  });

  // Handle clear
  document.getElementById("clear").addEventListener("click", () => {
    currentValue = "0";
    updateDisplay();
  });

  // Handle toggle sign
  document.getElementById("toggleSign").addEventListener("click", () => {
    currentSign = currentSign === "+" ? "-" : "+";
    updateDisplay();
  });

  // Handle unit selection
  document.querySelectorAll(".key.unit").forEach((key) => {
    key.addEventListener("click", () => {
      currentUnit = key.dataset.unit;
      updateDisplay();
    });
  });

  // Handle cancel
  document.getElementById("cancel").addEventListener("click", () => {
    document.getElementById("numericInputModal").style.display = "none";
  });

  // Handle submit
  document.getElementById("submit").addEventListener("click", () => {
    const finalValue = `${currentSign}${currentValue} ${currentUnit}`;
    console.log("Submitted value:", finalValue); // Replace with your logic
    document.getElementById("numericInputModal").style.display = "none";
  });

  // To open the modal and prepopulate values
  function openNumericInputModal(initialValue = "0", initialUnit = "m", initialSign = "+") {
    document.getElementById("numericInputModal").style.display = "flex";
    currentValue = initialValue;
    currentUnit = initialUnit;
    currentSign = initialSign;
    updateDisplay();
  }


  //**************** end


  // Reference to the container
  const container = document.getElementById("channels-container");

  // Function to create a channel
  function createChannel(channelId) {
    const channelContainer = document.getElementById("channels-container");

    const channelDiv = document.createElement("div");
    channelDiv.classList.add("channel");
    channelDiv.setAttribute("id", `channel-${channelId}`);

    channelDiv.innerHTML = `
        <div class="channel-header">ch${channelId}</div>
        <div class="field">
            <div class="field-label">FV:</div>
            <div id="fv-ch${channelId}" class="field-value">----</div>
            <div class="field-unit">V</div>
        </div>
        <div class="field">
            <div class="field-label">MI:</div>
            <div id="mi-ch${channelId}" class="field-value">----</div>
            <div id="mi-unit-ch${channelId}" class="field-unit">mA</div>
        </div>
        <div class="field">
            <div class="field-label">MV:</div>
            <div id="mv-ch${channelId}" class="field-value">----</div>
            <div class="field-unit">V</div>
        </div>
        <div class="field">
            <div class="field-label">CL:</div>
            <div id="cl-ch${channelId}" class="field-value">----</div>
            <div id="cl-unit-ch${channelId}" class="field-unit">mA</div>
        </div>
        <div class="field">
            <div class="field-label">CH:</div>
            <div id="ch-ch${channelId}" class="field-value">----</div>
            <div id="ch-unit-ch${channelId}" class="field-unit">mA</div>
        </div>
        <div id="state-ch${channelId}" class="status off">off</div>
        <div class="settings">
            <button>Remote Sense</button>
            <button class="vbutton">10V</button>
            <button class="range-button">2mA</button>
            <button>FVMI</button>
        </div>
    `;
    channelContainer.appendChild(channelDiv);

    // Attach events to the dynamically created range button
    attachRangeButtonEvents(channelId);
    attachKeypadEvents(channelId);
  }

 // Function to update a channel based on WebSocket data
    function updateChannel(data) {
        const channelId = data.ch;
        for (const key in data) {
            if (key !== "type" && key !== "ch") {
                const field = document.getElementById(`${key}-ch${channelId}`);
                if (field) {
                    field.textContent = data[key];
                }
            }
        }
    }

    // Create 4 channels dynamically
    for (let i = 0; i < 4; i++) {
      createChannel(i);
    }


//  ///////////////////////////////////////////////////////////
//  //  Construct Channel
//  ///////////////////////////////////////////////////////////
//  // Channel configuration data
//  const channels = [
//      { id: 0, status: 'on', fv: '10.000', mi: '0.1219', mv: '9.99998', cl: '0.000', ch: '0.200', unitFV: 'V', unitMI: 'mA', unitMV: 'V', unitCL: 'mA', unitCH: 'mA' },
//      { id: 1, status: 'on', fv: '', fi: '110.00', mv: '0.50010', mi: '100.01', cl: '-1.000', ch: '0.500', unitFV: '', unitFI: 'µA', unitMV: 'V', unitMI: 'µA', unitCL: 'V', unitCH: 'V' },
//      { id: 2, status: 'off', fi: '200.00', mv: '---', mi: '0.000', cl: '210.0', ch: '-210.0', unitFI: 'mA', unitMV: 'V', unitMI: 'mA', unitCL: 'mA', unitCH: 'mA' },
//      { id: 3, status: 'off', fv: '10.000', mi: '---', mv: '0.00000', cl: '-5.0', ch: '5.0', unitFV: 'V', unitMI: 'mA', unitMV: 'V', unitCL: 'V', unitCH: 'V' }
//  ];
//
//  // Function to create a channel HTML template with editable fields
//  function createChannel(channel) {
//      return `
//          <div class="channel" id="channel-${channel.id}">
//              <div class="channel-header">ch${channel.id}</div>
//              ${channel.fv !== undefined ? `
//                  <div class="field-label">FV:</div>
//                  <div class="field-value" data-type="fv" data-channel="${channel.id}">${channel.fv}</div>
//                  <div class="field-unit">${channel.unitFV}</div>
//              ` : ''}
//              ${channel.fi !== undefined ? `
//                  <div class="field-label">FI:</div>
//                  <div class="field-value" data-type="fi" data-channel="${channel.id}">${channel.fi}</div>
//                  <div class="field-unit">${channel.unitFI}</div>
//              ` : ''}
//              <div class="field-label">MV:</div>
//              <div class="field-value" data-type="mv" data-channel="${channel.id}">${channel.mv}</div>
//              <div class="field-unit">${channel.unitMV}</div>
//
//              <div class="field-label">MI:</div>
//              <div class="field-value" data-type="mi" data-channel="${channel.id}">${channel.mi}</div>
//              <div class="field-unit">${channel.unitMI}</div>
//
//              <div class="field-label">CL:</div>
//              <div class="field-value" data-type="cl" data-channel="${channel.id}">${channel.cl}</div>
//              <div class="field-unit">${channel.unitCL}</div>
//
//              <div class="field-label">CH:</div>
//              <div class="field-value" data-type="ch" data-channel="${channel.id}">${channel.ch}</div>
//              <div class="field-unit">${channel.unitCH}</div>
//
//              <div class="status ${channel.status}" id="ch${channel.id}-status">${channel.status}</div>
//
//              <div class="settings">
//                  <button id="ch${channel.id}-remote-sense">Remote Sense</button>
//                  <button id="ch${channel.id}-config-10v">10V</button>
//                  <button id="ch${channel.id}-config-2ma">2mA</button>
//                  <button id="ch${channel.id}-config-fvmi">FVMI</button>
//              </div>
//          </div>
//      `;
//  }
//
//  // Render all channels
//  const container = document.getElementById('channels-container');
//  channels.forEach(channel => {
//      container.innerHTML += createChannel(channel);
//  });
//
//  // Add event listeners to make fields editable
//  document.querySelectorAll('.field-value').forEach(field => {
//      field.addEventListener('click', function() {
//          const currentValue = this.textContent;
//          const input = document.createElement('input');
//          input.type = 'text';
//          input.value = currentValue;
//          input.style.width = `${Math.max(currentValue.length, 4)}ch`; // Adjust width to fit content
//          this.replaceWith(input);
//          input.focus();
//
//          // Save changes on blur or Enter key
//          input.addEventListener('blur', () => saveEdit(this, input.value));
//          input.addEventListener('keydown', (e) => {
//              if (e.key === 'Enter') saveEdit(this, input.value);
//          });
//      });
//  });
//
//  // Function to save edited value
//  function saveEdit(field, newValue) {
//      const channelId = field.getAttribute('data-channel');
//      const type = field.getAttribute('data-type');
//
//      // Update field display
//      field.textContent = newValue;
//      field.parentNode.replaceChild(field, field.nextSibling);
//
//      // Update the internal data model
//      const channel = channels.find(ch => ch.id == channelId);
//      if (channel) {
//          channel[type] = newValue;
//          console.log(`Updated channel ${channelId} ${type} to ${newValue}`);
//      }
//  }
});


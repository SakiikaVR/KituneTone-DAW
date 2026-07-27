class StereoBalancePlugin extends PluginBase {
    constructor() {
        super('Stereo Balance', 'Adjusts the balance between left and right channels');
        this.bl = 0;  // bl: Balance (formerly balance) - -1 (full left) to +1 (full right), 0 is center

        // Register processor function
        this.registerProcessor(`
            if (!parameters.enabled) return data;
            if (parameters.channelCount < 2) return data; // Only process stereo signals
            
            // Map shortened parameter names to their original names for clarity
            const { 
                bl: balance,    // bl: Balance (formerly balance)
                channelCount, blockSize, type 
            } = parameters;
            
            // Process left and right channels
            const leftGain = balance <= 0 ? 1 : 1 - balance;
            const rightGain = balance >= 0 ? 1 : 1 + balance;
            
            // Left channel (first block)
            for (let i = 0; i < parameters.blockSize; i++) {
                data[i] *= leftGain;
            }
            
            // Right channel (next block)
            const len = data.length;
            for (let i = parameters.blockSize; i < len; i++) {
                data[i] *= rightGain;
            }
            
            return data;
        `);
    }

    // Set parameters
    setParameters(params) {
        if (params.bl !== undefined) {
            this.bl = params.bl < -1 ? -1 : (params.bl > 1 ? 1 : params.bl);
        }
        if (params.enabled !== undefined) {
            this.enabled = params.enabled;
        }
        this.updateParameters();
    }

    // Set balance value (-1 to +1)
    setBl(value) {
        this.setParameters({ bl: value });
    }

    getParameters() {
        return {
            type: this.constructor.name,
            bl: this.bl,
            enabled: this.enabled
        };
    }

    // Create UI elements for the plugin
    createUI() {
        const container = document.createElement('div');
        container.className = 'plugin-parameter-ui';

        // Use helper to create balance control
        const balanceControl = this.createParameterControl(
            'Balance', -100, 100, 1, this.bl * 100, 
            (value) => this.setBl(value / 100), '%' // Divide by 100 in setter
        );
        container.appendChild(balanceControl);
        
        return container;
    }
}

// Register the plugin
window.StereoBalancePlugin = StereoBalancePlugin;

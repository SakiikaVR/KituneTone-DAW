class MutePlugin extends PluginBase {
    constructor() {
        super('Mute', 'Mutes the audio signal by filling the output with silence');

        // Register processor function
        this.registerProcessor(`
            if (!parameters.enabled) return data;
            const { 
                channelCount, blockSize 
            } = parameters;
            
            const len = data.length;
            for (let i = 0; i < len; i++) {
                data[i] = 0;
            }
            return data;
        `);
    }

    // Set parameters
    setParameters(params) {
        if (params.enabled !== undefined) {
            this.enabled = params.enabled;
        }
        this.updateParameters();
    }

    getParameters() {
        return {
            type: this.constructor.name,
            enabled: this.enabled
        };
    }

    // Create UI elements for the plugin
    createUI() {
        const container = document.createElement('div');
        container.className = 'mute-plugin-ui plugin-parameter-ui';

        return container;
    }
}

// Register the plugin
window.MutePlugin = MutePlugin; 
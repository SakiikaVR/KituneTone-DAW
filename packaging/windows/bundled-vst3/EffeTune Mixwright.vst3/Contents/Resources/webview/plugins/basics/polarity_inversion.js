class PolarityInversionPlugin extends PluginBase {
    constructor() {
        super('Polarity Inversion', 'Inverts the polarity of the audio signal');

        // Register processor function
        this.registerProcessor(`
            if (!parameters.enabled) return data;
            const { 
                channelCount, blockSize 
            } = parameters;
            
            const len = data.length;
            for (let i = 0; i < len; i++) {
                data[i] = -data[i];
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
        container.className = 'polarity-inversion-plugin-ui plugin-parameter-ui';

        return container;
    }
}

// Register the plugin
window.PolarityInversionPlugin = PolarityInversionPlugin;

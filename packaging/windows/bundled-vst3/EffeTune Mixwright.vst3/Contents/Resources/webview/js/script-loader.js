// Script and CSS loading utilities
export async function loadScript(src) {
    // Check if it's a bundle request
    if (Array.isArray(src)) {
        // Load all scripts individually in sequence
        for (const url of src) {
            await new Promise((resolve, reject) => {
                const script = document.createElement('script');
                script.src = url;
                script.onload = resolve;
                script.onerror = (error) => {
                    console.error(`Failed to load script: ${url}`, error);
                    resolve(); // Continue loading other scripts even if one fails
                };
                document.head.appendChild(script);
            });
        }
        return Promise.resolve();
    }
    
    // Single script loading
        return new Promise((resolve) => {
            const script = document.createElement('script');
            script.src = src;
            script.onload = resolve;
            script.onerror = (error) => {
                console.error(`Failed to load script: ${src}`, error);
                resolve(); // Continue even if a single script fails
            };
            document.head.appendChild(script);
        });
}

export async function loadCSS(src) {
    // Check if it's a bundle request
    if (Array.isArray(src)) {
        // Load all CSS files individually
        const promises = src.map(url => {
            return new Promise((resolve, reject) => {
                const link = document.createElement('link');
                link.rel = 'stylesheet';
                link.href = url;
                link.onload = resolve;
                link.onerror = (error) => {
                    console.error(`Failed to load CSS: ${url}`, error);
                    resolve(); // Continue loading other CSS even if one fails
                };
                document.head.appendChild(link);
            });
        });
        
        return Promise.all(promises);
    }
    
    // Single CSS loading
        return new Promise((resolve) => {
            const link = document.createElement('link');
            link.rel = 'stylesheet';
            link.href = src;
            link.onload = resolve;
            link.onerror = (error) => {
                console.error(`Failed to load CSS: ${src}`, error);
                resolve(); // Continue even if a single CSS file fails
            };
            document.head.appendChild(link);
        });
}

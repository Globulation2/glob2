package org.globulation.glob2;

import android.net.http.X509TrustManagerExtensions;
import java.io.ByteArrayInputStream;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

/** Uses Android's current app/system trust policy; never an accept-all verifier. */
final class CertificateTrust {
    static boolean verify(byte[][] encoded, String hostname) {
        try {
            if (encoded == null || encoded.length == 0 || encoded.length > 16 || hostname == null || hostname.isEmpty()) return false;
            CertificateFactory certificates = CertificateFactory.getInstance("X.509");
            X509Certificate[] chain = new X509Certificate[encoded.length];
            int total = 0;
            for (int i = 0; i < encoded.length; ++i) {
                if (encoded[i] == null || encoded[i].length == 0 || encoded[i].length > 65536) return false;
                total += encoded[i].length;
                if (total > 131072) return false;
                ByteArrayInputStream input = new ByteArrayInputStream(encoded[i]);
                chain[i] = (X509Certificate) certificates.generateCertificate(input);
                if (input.available() != 0) return false;
            }
            TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
            factory.init((KeyStore) null);
            for (TrustManager manager : factory.getTrustManagers()) {
                if (manager instanceof X509TrustManager) {
                    new X509TrustManagerExtensions((X509TrustManager) manager).checkServerTrusted(
                        chain, chain[0].getPublicKey().getAlgorithm(), hostname);
                    return true; // The C++ boundary separately checks DNS/IP SAN identity.
                }
            }
        } catch (Exception failure) { return false; }
        return false;
    }
}
